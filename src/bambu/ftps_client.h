#pragma once
/**
 * ftps_client.h
 *
 * Minimal implicit-FTPS client for downloading Bambu Lab printer thumbnails.
 *
 * TLS strategy — ctrl + data session sharing via raw mbedTLS:
 *
 *   Control channel: BambuTlsClient — full TLS handshake → session saved
 *   Data channel:    BambuTlsClient — TLS handshake with session RESUMED
 *
 * Why session resumption matters:
 *   Bambu's proftpd mandates "session reuse" (RFC 4217 §9): the data channel
 *   TLS handshake must present the same session ID as the control channel.
 *   If it doesn't, the server returns "522 SSL connection failed: session
 *   reuse required" and drops the data connection.
 *
 *   WiFiClientSecure (Arduino ESP32 3.x) no longer exposes SSLSession /
 *   setSession(), so we use mbedTLS directly via BambuTlsClient which gives
 *   us mbedtls_ssl_get_session() / mbedtls_ssl_set_session().
 *
 * Why PSRAM for the data channel:
 *   Two simultaneous mbedTLS sessions would OOM internal SRAM (~40 KB each).
 *   BambuTlsClient::connect(..., use_psram=true) redirects all mbedTLS heap
 *   allocations to the 8 MB PSRAM for the duration of the data handshake.
 *
 * Transfer flow:
 *   1. CWD <dir>           (ctrl TLS)
 *   2. PASV                (ctrl TLS) → data ip:port
 *   3. RETR / LIST         (ctrl TLS) — don't read reply yet
 *   4. BambuTlsClient connect to data ip:port, resuming ctrl session
 *   5. Wait for 150/125 on ctrl
 *   6. Read data from data TLS channel
 *   7. Drain 226 from ctrl
 */

#include "bambu_tls_client.h"
#include "mbedtls/ssl.h"

static const int TIMEOUT_MS = 8000;

class FtpsClient {
public:
    void   begin(const char *ip, const char *access_code);
    size_t downloadThumbnail(const char *job_name,
                             const char *gcode_file,
                             uint8_t    *out_buf,
                             size_t      max_bytes);

private:
    char              _ip[40]   = {};
    char              _code[64] = {};

    // ── Ctrl channel ─────────────────────────────────────────────────────────
    BambuTlsClient   *_ctrl        = nullptr;

    // ── Shared TLS session ────────────────────────────────────────────────────
    // Saved from _ctrl after every successful handshake.
    // Passed to every data-channel BambuTlsClient::connect() so it can present
    // the same session ID in its ClientHello → satisfies "session reuse required".
    mbedtls_ssl_session _session   = {};
    bool                _has_session = false;

    bool   _connectControl();
    bool   _ensureControl();
    bool   _login();
    bool   _sendCmd(const char *cmd, int expected_code,
                    char *reply = nullptr, size_t reply_sz = 0);
    bool   _parsePasv(const char *reply, char *data_ip, uint16_t &data_port);
    void   _drainCtrl(uint32_t timeout_ms = 3000);

    // Returns a connected BambuTlsClient* (caller must delete); nullptr on error.
    BambuTlsClient *_openData(const char *retr_path);

    size_t _downloadFile(const char *path, uint8_t *buf, size_t max_bytes);
    size_t _extractFromMf(const char *path, uint8_t *out_buf, size_t max_bytes);
    void   _listDir(const char *dir);
    void   _disconnect();
    void   _freeCtrl();
};
