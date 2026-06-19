#pragma once
/**
 * ftps_client.h
 *
 * Minimal implicit-FTPS client for downloading Bambu Lab printer thumbnails.
 *
 * The thumbnail is embedded inside the .3mf job file as Metadata/thumbnail.png
 * (the .3mf is a ZIP archive). We stream-parse the ZIP local-file-header chain
 * until the thumbnail entry is found, then inflate it with zlib raw-deflate.
 *
 * Connection strategy — keep ctrl alive while data channel is open:
 *   1. Control TLS: login, PROT P, PASV, RETR/LIST (ctrl stays alive)
 *   2. Data TLS: connect to PASV address → wait for 150 on ctrl → read data
 *   3. After data closes: drain 226/error from ctrl → ctrl ready for next command
 *
 * NOTE: freeing ctrl BEFORE connecting data causes the Bambu server to abort
 * the pending transfer (it sees the control connection close as an abort signal).
 * ESP32-S3 + PSRAM can hold two simultaneous TLS contexts without issue.
 */

#include <WiFiClientSecure.h>

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
    WiFiClientSecure *_ctrl     = nullptr;

    bool              _connectControl();
    bool              _ensureControl();
    bool              _login();
    bool              _sendCmd(const char *cmd, int expected_code,
                               char *reply = nullptr, size_t reply_sz = 0);
    bool              _parsePasv(const char *reply, char *data_ip, uint16_t &data_port);
    void              _drainCtrl(uint32_t timeout_ms = 3000);  // consume 226/error after transfer
    WiFiClientSecure *_openData(const char *retr_path);        // heap-alloc; caller deletes
    size_t            _downloadFile(const char *path, uint8_t *buf, size_t max_bytes);
    size_t            _extractFromMf(const char *path, uint8_t *out_buf, size_t max_bytes);
    void              _listDir(const char *dir);               // logs directory contents
    void              _disconnect();
    void              _freeCtrl();
};
