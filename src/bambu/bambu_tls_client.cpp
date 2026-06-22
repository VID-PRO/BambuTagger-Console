#include "bambu_tls_client.h"
#include <esp_heap_caps.h>

// mbedtls/net_sockets.h is not exposed in ESP-IDF Arduino builds; define the
// two BIO error codes we need using their canonical numeric values.
#ifndef MBEDTLS_ERR_NET_SEND_FAILED
#  define MBEDTLS_ERR_NET_SEND_FAILED  (-0x004E)   // -78
#endif
#ifndef MBEDTLS_ERR_NET_RECV_FAILED
#  define MBEDTLS_ERR_NET_RECV_FAILED  (-0x004C)   // -76
#endif

// ═══════════════════════════════════════════════════════════════════════════════
// PSRAM allocator
//
// heap_caps_malloc / heap_caps_free handle both DRAM and PSRAM pointers on
// ESP32 — it is safe to free PSRAM allocations with heap_caps_free even after
// the platform allocator has been restored to the default calloc/free.
// ═══════════════════════════════════════════════════════════════════════════════
void *ps_mbedtls_calloc(size_t n, size_t sz) {
    size_t total = n * sz;
    void  *p     = heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p)  memset(p, 0, total);
    else    p = calloc(n, sz);   // fall back to internal SRAM if PSRAM full
    return p;
}
void ps_mbedtls_free(void *p) { heap_caps_free(p); }

// ═══════════════════════════════════════════════════════════════════════════════
// mbedTLS I/O callbacks  (WiFiClient as TCP transport)
// ═══════════════════════════════════════════════════════════════════════════════
static int _bio_send(void *ctx, const unsigned char *buf, size_t len) {
    WiFiClient *c = static_cast<WiFiClient *>(ctx);
    size_t sent   = c->write(buf, len);
    return (sent > 0) ? (int)sent : MBEDTLS_ERR_NET_SEND_FAILED;
}

static int _bio_recv(void *ctx, unsigned char *buf, size_t len) {
    WiFiClient *c = static_cast<WiFiClient *>(ctx);
    // Brief spin so the handshake loop doesn't busy-wait too hard.
    uint32_t t = millis();
    while (!c->available()) {
        if (!c->connected()) return MBEDTLS_ERR_NET_RECV_FAILED;
        if (millis() - t > 100) return MBEDTLS_ERR_SSL_WANT_READ;
        delay(5);
    }
    int n = c->read(buf, len);
    return (n > 0) ? n : MBEDTLS_ERR_NET_RECV_FAILED;
}

// ═══════════════════════════════════════════════════════════════════════════════
// BambuTlsClient
// ═══════════════════════════════════════════════════════════════════════════════
BambuTlsClient::BambuTlsClient() {}

BambuTlsClient::~BambuTlsClient() { stop(); }

bool BambuTlsClient::connect(const char *host, uint16_t port,
                              mbedtls_ssl_session *session,
                              uint32_t timeout_ms, bool use_psram) {
    stop();

    // ── TCP layer ────────────────────────────────────────────────────────────
    _tcp.setTimeout(timeout_ms / 1000);
    if (!_tcp.connect(host, port)) {
        log_w("BambuTls: TCP connect failed → %s:%d", host, port);
        return false;
    }

    // ── Initialise mbedTLS contexts ──────────────────────────────────────────
    mbedtls_ssl_init(&_ssl);
    mbedtls_ssl_config_init(&_conf);
    mbedtls_entropy_init(&_entropy);
    mbedtls_ctr_drbg_init(&_drbg);
    _tls_init = true;

    int ret;
    const char *pers = "bambu_ftps";

    ret = mbedtls_ctr_drbg_seed(&_drbg, mbedtls_entropy_func, &_entropy,
                                 (const uint8_t *)pers, strlen(pers));
    if (ret != 0) { log_e("BambuTls: drbg_seed %d", ret); goto fail; }

    ret = mbedtls_ssl_config_defaults(&_conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) { log_e("BambuTls: config_defaults %d", ret); goto fail; }

    // Insecure (no cert check) — same as WiFiClientSecure::setInsecure()
    mbedtls_ssl_conf_authmode(&_conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&_conf, mbedtls_ctr_drbg_random, &_drbg);

    ret = mbedtls_ssl_setup(&_ssl, &_conf);
    if (ret != 0) { log_e("BambuTls: ssl_setup %d", ret); goto fail; }

    // SNI (server name indication)
    mbedtls_ssl_set_hostname(&_ssl, host);

    // Wire up WiFiClient as the I/O transport
    mbedtls_ssl_set_bio(&_ssl, &_tcp, _bio_send, _bio_recv, nullptr);

    // ── Session resumption (must be set BEFORE handshake) ────────────────────
    // Passing the ctrl channel's saved mbedtls_ssl_session makes mbedTLS
    // include that session ID in the ClientHello.  The Bambu proftpd server
    // sees the matching session ID and accepts the data connection ("session
    // reuse required" check passes).  If the server's session cache has
    // expired it falls back to a full handshake automatically.
    if (session) {
        ret = mbedtls_ssl_set_session(&_ssl, session);
        if (ret != 0) log_w("BambuTls: set_session %d → full handshake", ret);
    }

    // ── TLS handshake ────────────────────────────────────────────────────────
    {
        uint32_t t = millis();
        do {
            ret = mbedtls_ssl_handshake(&_ssl);
            if (millis() - t > timeout_ms) {
                log_e("BambuTls: handshake timeout");
                goto fail;
            }
            if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                ret == MBEDTLS_ERR_SSL_WANT_WRITE) delay(10);
        } while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                 ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    }

    if (ret != 0) {
        char errbuf[80];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_e("BambuTls: handshake failed %d %s", ret, errbuf);
        _cleanup();
        return false;
    }

    // Set Stream timeout for readStringUntil() etc.
    setTimeout(timeout_ms);

    _connected = true;
    return true;

fail:
    _cleanup();
    return false;
}

bool BambuTlsClient::saveSession(mbedtls_ssl_session *out) {
    if (!_tls_init || !_connected) return false;
    return mbedtls_ssl_get_session(&_ssl, out) == 0;
}

// ── Stream interface ─────────────────────────────────────────────────────────

int BambuTlsClient::available() {
    if (!_connected) return 0;
    // Return already-decrypted bytes in mbedTLS's application buffer.
    size_t pending = mbedtls_ssl_get_bytes_avail(&_ssl);
    if (pending > 0) return (int)pending;
    // TCP layer may have encrypted data ready to decrypt.
    return (_tcp.available() > 0) ? 1 : 0;
}

int BambuTlsClient::read() {
    if (!_connected) return -1;
    uint8_t b;
    int n = mbedtls_ssl_read(&_ssl, &b, 1);
    if (n == 1) return (int)(uint8_t)b;
    if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) _connected = false;
    return -1;
}

int BambuTlsClient::read(uint8_t *buf, size_t n) {
    if (!_connected || n == 0) return 0;
    int r = mbedtls_ssl_read(&_ssl, buf, n);
    if (r > 0) return r;
    if (r == MBEDTLS_ERR_SSL_WANT_READ) return 0;   // partial record; caller retries
    if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) _connected = false;
    return 0;
}

size_t BambuTlsClient::write(uint8_t b) { return write(&b, 1); }

size_t BambuTlsClient::write(const uint8_t *buf, size_t n) {
    if (!_connected || n == 0) return 0;
    size_t sent = 0;
    while (sent < n) {
        int r = mbedtls_ssl_write(&_ssl, buf + sent, n - sent);
        if (r > 0) sent += r;
        else if (r != MBEDTLS_ERR_SSL_WANT_WRITE) break;
    }
    return sent;
}

bool BambuTlsClient::connected() {
    return _connected && _tcp.connected();
}

void BambuTlsClient::stop() {
    if (_tls_init && _connected) mbedtls_ssl_close_notify(&_ssl);
    _cleanup();
    _tcp.stop();
}

void BambuTlsClient::_cleanup() {
    if (_tls_init) {
        mbedtls_ssl_free(&_ssl);
        mbedtls_ssl_config_free(&_conf);
        mbedtls_entropy_free(&_entropy);
        mbedtls_ctr_drbg_free(&_drbg);
        _tls_init = false;
    }
    _connected = false;
}
