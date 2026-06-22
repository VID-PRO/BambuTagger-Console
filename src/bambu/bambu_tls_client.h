#pragma once
/**
 * BambuTlsClient
 *
 * Minimal TLS client built directly on mbedTLS + WiFiClient (plain TCP).
 *
 * Why not WiFiClientSecure?
 *   Arduino ESP32 3.x rewrote WiFiClientSecure to use esp-tls internally and
 *   removed the SSLSession / setSession() API that existed in 2.x.  There is
 *   no longer a public way to copy a TLS session from the control channel to
 *   the data channel, which is required by Bambu Lab's proftpd:
 *     "522 SSL connection failed: session reuse required"
 *
 * Why raw mbedTLS?
 *   mbedTLS is the underlying TLS library in every ESP-IDF version and is
 *   always available.  Using it directly gives us:
 *     • mbedtls_ssl_get_session() — save ctrl-channel session after handshake
 *     • mbedtls_ssl_set_session() — resume saved session on data channel
 *     • mbedtls_platform_set_calloc_free() — redirect data-channel allocations
 *       to PSRAM so both TLS contexts fit without OOM
 *
 * Extends Stream so print() / readStringUntil() work on the ctrl channel
 * without changing any FtpsClient code that uses those methods.
 */

#include <Arduino.h>
#include <WiFi.h>
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/platform.h"
#include "mbedtls/error.h"

// ── PSRAM-aware mbedTLS allocator ──────────────────────────────
// Called via mbedtls_platform_set_calloc_free() to route mbedTLS
// heap allocations to PSRAM, preserving internal SRAM for
// esp-aes DMA buffers.  Falls back to internal SRAM when PSRAM
// is full.
void *ps_mbedtls_calloc(size_t n, size_t sz);
void  ps_mbedtls_free(void *p);

class BambuTlsClient : public Stream {
public:
    BambuTlsClient();
    ~BambuTlsClient();

    /**
     * Connect to host:port via TLS.
     *
     * @param host        hostname or dotted-IP string (also used for SNI)
     * @param port        TCP port
     * @param session     Session to resume, or nullptr for a full handshake.
     *                    Obtain from saveSession() after a successful connect.
     * @param timeout_ms  Overall timeout in milliseconds (TCP + TLS handshake).
     * @param use_psram   Route mbedTLS heap to PSRAM during this connect.
     *                    Use true for the data channel to avoid OOM when the
     *                    ctrl-channel TLS context is already in internal SRAM.
     * @return true on success.
     */
    bool connect(const char *host, uint16_t port,
                 mbedtls_ssl_session *session = nullptr,
                 uint32_t timeout_ms = 10000,
                 bool use_psram = false);

    /**
     * Copy the current TLS session into *out (caller must zero-initialise
     * *out before the first call).  Call mbedtls_ssl_session_free(out)
     * before calling saveSession() again and before destruction.
     * @return true on success.
     */
    bool saveSession(mbedtls_ssl_session *out);

    // ── Stream interface (gives print / println / readStringUntil for free) ──
    int    available() override;
    int    read()      override;          // single byte; -1 on error / closed
    int    peek()      override { return -1; }
    void   flush()     override {}
    size_t write(uint8_t b)                    override;
    size_t write(const uint8_t *buf, size_t n) override;

    // ── Extra: non-blocking multi-byte read (mirrors WiFiClient::read) ────────
    int read(uint8_t *buf, size_t n);

    bool connected();
    void stop();

private:
    void _cleanup();

    WiFiClient               _tcp;
    mbedtls_ssl_context      _ssl;
    mbedtls_ssl_config       _conf;
    mbedtls_entropy_context  _entropy;
    mbedtls_ctr_drbg_context _drbg;
    bool _tls_init  = false;
    bool _connected = false;
};
