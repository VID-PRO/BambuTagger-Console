#include "ftps_client.h"
#include "config.h"
extern "C" {
#include "puff/puff.h"
}

// ═════════════════════════════════════════════════════════════════════════════
// Stream helpers — operate on BambuTlsClient (data channel)
//
// s_read: blocking read of exactly n bytes with timeout
// s_skip: blocking skip of exactly n bytes with timeout
// ═════════════════════════════════════════════════════════════════════════════
static bool s_read(BambuTlsClient &c, uint8_t *buf, size_t n, uint32_t tms) {
    size_t   got = 0;
    uint32_t t   = millis();
    while (got < n) {
        if (millis() - t > tms) return false;
        if (c.available()) {
            int r = c.read(buf + got, n - got);
            if (r > 0) { got += r; t = millis(); }
        } else delay(1);
    }
    return true;
}

static bool s_skip(BambuTlsClient &c, uint32_t n, uint32_t tms) {
    uint8_t  tmp[256];
    uint32_t done = 0;
    uint32_t t    = millis();
    while (done < n) {
        if (millis() - t > tms) return false;
        uint32_t chunk = min((uint32_t)sizeof(tmp), n - done);
        if (c.available()) {
            int r = c.read(tmp, chunk);
            if (r > 0) { done += r; t = millis(); }
        } else delay(1);
    }
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// Control-channel helpers
// ═════════════════════════════════════════════════════════════════════════════

void FtpsClient::begin(const char *ip, const char *access_code) {
    strncpy(_ip,   ip,          sizeof(_ip)   - 1);
    strncpy(_code, access_code, sizeof(_code) - 1);
}

void FtpsClient::_freeCtrl() {
    if (_ctrl) {
        if (_ctrl->connected()) { _ctrl->flush(); _ctrl->stop(); }
        delete _ctrl;
        _ctrl = nullptr;
    }
}

void FtpsClient::_disconnect() {
    if (_ctrl && _ctrl->connected()) {
        _ctrl->print("QUIT\r\n");
        _ctrl->flush();
        delay(100);
    }
    _freeCtrl();
}

bool FtpsClient::_connectControl() {
    _freeCtrl();
    _ctrl = new BambuTlsClient();

    log_i("FTPS connecting to %s:%d", _ip, BAMBU_FTP_PORT);

    // On reconnect try to resume the previous ctrl session (fast handshake).
    // On first connect _has_session is false → nullptr → full handshake.
    // Either way we always save the resulting session for data channels.
    bool ok = _ctrl->connect(_ip, BAMBU_FTP_PORT,
                              _has_session ? &_session : nullptr,
                              TIMEOUT_MS, /*use_psram=*/true);
    if (!ok) {
        log_w("FTPS TCP/TLS connect failed");
        _freeCtrl();
        return false;
    }
    log_i("FTPS TLS handshake OK");

    // ── Save ctrl session so data channels can resume it ─────────────────────
    // mbedtls_ssl_session_free is safe to call on a zeroed struct (first time)
    // and correctly frees any allocations from the previous save.
    mbedtls_ssl_session_free(&_session);
    memset(&_session, 0, sizeof(_session));
    _has_session = _ctrl->saveSession(&_session);
    if (_has_session) log_i("FTPS ctrl session saved for data-channel resumption");

    // ── Wait for FTP 220 banner ───────────────────────────────────────────────
    char reply[128] = {};
    unsigned long t = millis();
    bool got_banner = false;
    while (millis() - t < (unsigned long)TIMEOUT_MS) {
        if (_ctrl->available()) {
            _ctrl->readStringUntil('\n').toCharArray(reply, sizeof(reply));
            if (strncmp(reply, "220", 3) == 0 && reply[3] == ' ') {
                got_banner = true;
                break;
            }
        }
        delay(10);
    }
    if (!got_banner) log_w("FTPS no 220 banner");
    return got_banner;
}

bool FtpsClient::_login() {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "USER %s", BAMBU_MQTT_USER);
    if (!_sendCmd(cmd, 331)) return false;
    snprintf(cmd, sizeof(cmd), "PASS %s", _code);
    return _sendCmd(cmd, 230);
}

bool FtpsClient::_ensureControl() {
    if (_ctrl && _ctrl->connected()) return true;
    if (!_connectControl()) return false;
    if (!_login()) { _disconnect(); return false; }
    _sendCmd("PBSZ 0", 200);
    _sendCmd("PROT P", 200);   // encrypted data channel — Bambu rejects PROT C (522)
    _sendCmd("TYPE I", 200);
    return true;
}

bool FtpsClient::_sendCmd(const char *cmd, int expected,
                           char *reply_out, size_t reply_sz) {
    String line = String(cmd) + "\r\n";
    _ctrl->print(line);
    _ctrl->flush();

    char reply[256] = {};
    unsigned long t = millis();
    while (millis() - t < (unsigned long)TIMEOUT_MS) {
        if (_ctrl->available()) {
            _ctrl->readStringUntil('\n').toCharArray(reply, sizeof(reply));
            int code = atoi(reply);
            if (reply[3] == ' ' && code > 0) {
                log_i("FTPS < %s", reply);
                if (reply_out && reply_sz) strncpy(reply_out, reply, reply_sz - 1);
                return code == expected;
            }
        }
        delay(5);
    }
    log_w("FTPS timeout after '%s' (last: %.30s)", cmd, reply);
    return false;
}

bool FtpsClient::_parsePasv(const char *reply, char *data_ip, uint16_t &data_port) {
    const char *p = strchr(reply, '(');
    if (!p) return false;
    int h1, h2, h3, h4, p1, p2;
    if (sscanf(p + 1, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6)
        return false;
    sprintf(data_ip, "%d.%d.%d.%d", h1, h2, h3, h4);
    data_port = (uint16_t)(p1 * 256 + p2);
    return true;
}

void FtpsClient::_drainCtrl(uint32_t timeout_ms) {
    if (!_ctrl || !_ctrl->connected()) return;
    uint32_t t = millis();
    while (millis() - t < timeout_ms) {
        if (_ctrl->available()) {
            char buf[128] = {};
            _ctrl->readStringUntil('\n').toCharArray(buf, sizeof(buf));
            int code = atoi(buf);
            if (buf[3] == ' ' && code > 0) {
                log_i("FTPS < %s", buf);
                if (code == 226 || code >= 400) return;
            }
            t = millis();
        } else delay(5);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// _openData — CWD + PASV + RETR, then open TLS data channel with session reuse.
//
// Flow:
//   1. CWD to the directory of retr_path
//   2. PASV → data ip:port
//   3. Send RETR <filename> on ctrl (do NOT read reply yet)
//   4. Connect BambuTlsClient to data ip:port, resuming the ctrl session
//      (use_psram=true keeps data TLS out of internal SRAM)
//   5. Wait for 150/125 on ctrl
//   6. Return BambuTlsClient*; caller reads, stop()s, and deletes it,
//      then calls _drainCtrl() for the trailing 226.
// ═════════════════════════════════════════════════════════════════════════════
BambuTlsClient *FtpsClient::_openData(const char *retr_path) {
    // Split path into directory and filename
    char dir[128]   = "/";
    char fname[128] = {};
    const char *slash = strrchr(retr_path, '/');
    if (slash && slash > retr_path) {
        size_t dlen = slash - retr_path;
        if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
        strncpy(dir, retr_path, dlen);
        dir[dlen] = '\0';
        strncpy(fname, slash + 1, sizeof(fname) - 1);
    } else {
        strncpy(fname, (slash ? slash + 1 : retr_path), sizeof(fname) - 1);
    }

    // CWD to directory
    char cwd_cmd[160];
    snprintf(cwd_cmd, sizeof(cwd_cmd), "CWD %s", dir);
    if (!_sendCmd(cwd_cmd, 250)) {
        log_w("FTPS CWD failed: %s", dir);
        return nullptr;
    }

    char pasv_reply[128] = {};
    if (!_sendCmd("PASV", 227, pasv_reply, sizeof(pasv_reply))) return nullptr;

    char     data_ip[20] = {};
    uint16_t data_port   = 0;
    if (!_parsePasv(pasv_reply, data_ip, data_port)) return nullptr;

    // Send RETR, then peek the ctrl channel for ~200ms.
    // If the file does not exist the server sends 550 almost immediately and
    // never starts the TLS handshake on the data port — without this peek the
    // client would hang for the full TIMEOUT_MS before giving up.
    char cmd[192];
    snprintf(cmd, sizeof(cmd), "RETR %s", fname);
    _ctrl->print(String(cmd) + "\r\n");
    _ctrl->flush();

    bool pre_150 = false;
    {
        uint32_t pt = millis();
        while (millis() - pt < 200) {
            if (_ctrl->available()) {
                char peek[128] = {};
                _ctrl->readStringUntil('\n').toCharArray(peek, sizeof(peek));
                int code = atoi(peek);
                if (peek[3] == ' ' && code > 0) {
                    log_i("FTPS < %s", peek);
                    if (code >= 400) {
                        log_w("FTPS RETR pre-rejected (%d): %s", code, retr_path);
                        return nullptr;          // no TLS attempt needed
                    }
                    if (code == 150 || code == 125) pre_150 = true;
                }
                break;
            }
            delay(10);
        }
    }

    // ── Open TLS data channel with session resumption ─────────────────────────
    // use_psram=true: data-channel mbedTLS allocs go to PSRAM so both TLS
    // sessions (ctrl in SRAM, data in PSRAM) coexist without OOM.
    // _has_session: ctrl's mbedtls_ssl_session was saved after handshake;
    // the data channel presents that session ID in its ClientHello → proftpd
    // accepts it and does not return "522 session reuse required".
    BambuTlsClient *data = new BambuTlsClient();
    bool conn_ok = data->connect(data_ip, data_port,
                                 _has_session ? &_session : nullptr,
                                 TIMEOUT_MS, /*use_psram=*/true);
    if (!conn_ok) {
        log_w("FTPS data connect failed for %s", retr_path);
        delete data;
        // Send ABOR — without it the server is stuck waiting for the
        // data connection and will ignore subsequent control commands.
        _ctrl->print("ABOR\r\n"); _ctrl->flush();
        _drainCtrl(2000);
        return nullptr;
    }

    // Wait for 150/125 on ctrl (may already have it from the pre-peek above)
    char     reply[128] = {};
    uint32_t t          = millis();
    bool     started    = pre_150;
    while (!started && millis() - t < 5000) {
        if (_ctrl->available()) {
            _ctrl->readStringUntil('\n').toCharArray(reply, sizeof(reply));
            int code = atoi(reply);
            if (reply[3] == ' ' && code > 0) {
                log_i("FTPS < %s", reply);
                if (code == 150 || code == 125) { started = true; break; }
                if (code >= 400) {
                    log_w("FTPS RETR rejected (%d): %s", code, retr_path);
                    data->stop(); delete data;
                    return nullptr;
                }
            }
        }
        delay(5);
    }
    if (!started) {
        log_w("FTPS no 150 for %s", retr_path);
        data->stop(); delete data;
        _ctrl->print("ABOR\r\n"); _ctrl->flush();
        _drainCtrl(2000);
        return nullptr;
    }

    log_i("FTPS data open → %s", retr_path);
    return data;
}

// ═════════════════════════════════════════════════════════════════════════════
// Download a flat file into out_buf.
// ═════════════════════════════════════════════════════════════════════════════
size_t FtpsClient::_downloadFile(const char *path, uint8_t *buf, size_t max_bytes) {
    BambuTlsClient *data = _openData(path);
    if (!data) return 0;

    size_t   total      = 0;
    uint32_t t          = millis();
    const uint32_t RX_TIMEOUT = TIMEOUT_MS * 4;

    while ((data->connected() || data->available()) &&
           total < max_bytes &&
           millis() - t < RX_TIMEOUT) {
        if (data->available()) {
            int r = data->read(buf + total, max_bytes - total);
            if (r > 0) { total += r; t = millis(); taskYIELD(); }
        } else delay(1);
    }
    data->stop();
    delete data;
    _drainCtrl();
    return total;
}

// ═════════════════════════════════════════════════════════════════════════════
// Stream a .3mf (ZIP) and extract the embedded thumbnail PNG.
// Handles Bambu .3mf where every entry uses data-descriptor mode (comp_sz==0
// in the local file header, actual sizes follow the compressed data).
// ═════════════════════════════════════════════════════════════════════════════

// Scan forward byte-by-byte until a PK local-file (03 04), central-dir
// (01 02), or EOCD (05 06) signature is found.  Fills next_sig[4].
static bool s_scan_to_pk(BambuTlsClient &s, uint32_t max_scan,
                          uint32_t timeout_ms, uint8_t next_sig[4]) {
    uint8_t w[4] = {};
    int n = 0;
    for (uint32_t i = 0; i < max_scan; i++) {
        uint8_t b;
        if (!s_read(s, &b, 1, timeout_ms)) return false;
        w[0]=w[1]; w[1]=w[2]; w[2]=w[3]; w[3]=b;
        if (++n >= 4 && w[0]=='P' && w[1]=='K') {
            if ((w[2]==3&&w[3]==4)||(w[2]==1&&w[3]==2)||(w[2]==5&&w[3]==6)) {
                memcpy(next_sig, w, 4);
                return true;
            }
        }
    }
    return false;
}

size_t FtpsClient::_extractFromMf(const char *path, uint8_t *out_buf, size_t max_bytes) {
    BambuTlsClient *data = _openData(path);
    if (!data) return 0;

    const uint32_t MAX_SCAN     = 3UL * 1024 * 1024;
    const uint32_t DATA_TIMEOUT = TIMEOUT_MS * 6;
    uint32_t scanned  = 0;
    size_t   result   = 0;
    bool     have_sig = false;
    uint8_t  next_sig[4] = {};

    while (scanned < MAX_SCAN) {
        uint8_t sig[4];
        if (have_sig) {
            memcpy(sig, next_sig, 4);
            have_sig = false;
        } else {
            if (!s_read(*data, sig, 4, DATA_TIMEOUT)) { log_w("FTPS ZIP: timeout @ sig"); break; }
            scanned += 4;
        }

        if (sig[0]=='P' && sig[1]=='K' &&
            ((sig[2]==1 && sig[3]==2) || (sig[2]==5 && sig[3]==6))) {
            log_i("FTPS ZIP: central-dir reached, thumbnail not found");
            break;
        }
        if (!(sig[0]=='P' && sig[1]=='K' && sig[2]==3 && sig[3]==4)) {
            log_w("FTPS ZIP: unexpected sig %02x%02x%02x%02x at offset %u",
                  sig[0], sig[1], sig[2], sig[3], scanned - 4);
            break;
        }

        uint8_t hdr[26];
        if (!s_read(*data, hdr, 26, DATA_TIMEOUT)) break;
        scanned += 26;

        uint16_t flags     = hdr[2]  | (uint16_t(hdr[3])  << 8);
        uint16_t method    = hdr[4]  | (uint16_t(hdr[5])  << 8);
        uint32_t comp_sz   = hdr[14] | (uint32_t(hdr[15]) << 8)
                           | (uint32_t(hdr[16]) << 16) | (uint32_t(hdr[17]) << 24);
        uint16_t fname_len = hdr[22] | (uint16_t(hdr[23]) << 8);
        uint16_t extra_len = hdr[24] | (uint16_t(hdr[25]) << 8);

        char fname[256] = {};
        uint16_t to_read_n = (fname_len < 255) ? fname_len : 255;
        if (!s_read(*data, (uint8_t *)fname, to_read_n, DATA_TIMEOUT)) break;
        scanned += fname_len;
        if (fname_len > to_read_n && !s_skip(*data, fname_len - to_read_n, DATA_TIMEOUT)) break;
        if (!s_skip(*data, extra_len, DATA_TIMEOUT)) break;
        scanned += extra_len;

        bool is_thumb = (strncmp(fname, "Metadata/thumbnail", 18) == 0 ||
                         strncmp(fname, "Metadata/plate_1",   16) == 0);
        // Streaming = data-descriptor flag; comp_sz may be 0 in header
        bool streaming = (flags & 0x08) && (comp_sz == 0);

        if (!is_thumb) {
            if (streaming) {
                // Scan to next PK signature; bytes consumed are data + descriptor
                log_i("FTPS ZIP: scanning past '%s' (streaming entry)", fname);
                if (!s_scan_to_pk(*data, MAX_SCAN, DATA_TIMEOUT, next_sig)) break;
                have_sig = true;
            } else {
                if (!s_skip(*data, comp_sz, DATA_TIMEOUT)) break;
                scanned += comp_sz;
                if (flags & 0x08) { s_skip(*data, 16, DATA_TIMEOUT); scanned += 16; }
            }
            continue;
        }

        // ── Thumbnail entry ───────────────────────────────────────────────
        log_i("FTPS ZIP: found thumbnail '%s' method=%d comp_sz=%u streaming=%d",
              fname, method, comp_sz, streaming);

        if (streaming) {
            // Collect bytes into PSRAM until next PK sig (or stream end),
            // then strip the trailing data descriptor to get raw compressed data.
            const size_t MAX_COMP = 512 * 1024;
            uint8_t *cbuf = (uint8_t *)heap_caps_malloc(MAX_COMP,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!cbuf) cbuf = (uint8_t *)malloc(MAX_COMP);
            if (!cbuf) { log_w("FTPS ZIP: OOM streaming thumb"); break; }

            size_t  clen = 0;
            uint8_t w[4] = {};
            int     wn   = 0;

            while (clen < MAX_COMP) {
                uint8_t b;
                if (!s_read(*data, &b, 1, DATA_TIMEOUT)) break; // stream closed = end
                cbuf[clen++] = b;
                w[0]=w[1]; w[1]=w[2]; w[2]=w[3]; w[3]=b;
                if (++wn >= 4 && w[0]=='P' && w[1]=='K' &&
                    ((w[2]==3&&w[3]==4)||(w[2]==1&&w[3]==2)||(w[2]==5&&w[3]==6))) {
                    clen -= 4; // remove PK sig from buffer
                    memcpy(next_sig, w, 4);
                    have_sig = true;
                    break;
                }
            }

            // Strip data descriptor: [optional sig 50 4B 07 08] CRC(4) compSz(4) uncompSz(4)
            if (clen >= 16 &&
                cbuf[clen-16]==0x50 && cbuf[clen-15]==0x4B &&
                cbuf[clen-14]==0x07 && cbuf[clen-13]==0x08) {
                clen -= 16;
            } else if (clen >= 12) {
                clen -= 12;
            }
            log_i("FTPS ZIP: streaming thumb %u compressed bytes", clen);

            if (clen > 0) {
                if (method == 0) {
                    size_t n = min(clen, max_bytes);
                    memcpy(out_buf, cbuf, n);
                    result = n;
                } else if (method == 8) {
                    unsigned long destlen = (unsigned long)max_bytes;
                    unsigned long srclen  = (unsigned long)clen;
                    int ret = puff(out_buf, &destlen, cbuf, &srclen);
                    if (ret == 0) result = (size_t)destlen;
                    else log_w("FTPS inflate error %d (streaming)", ret);
                } else {
                    log_w("FTPS ZIP: unsupported method %d (streaming)", method);
                }
            }
            free(cbuf);

        } else {
            // Non-streaming: comp_sz is known in the local header
            if (comp_sz == 0 || comp_sz > 1024UL * 1024) {
                log_w("FTPS ZIP: thumbnail comp_sz=%u out of range", comp_sz);
                break;
            }
            if (method == 0) {
                size_t to_get = min((size_t)comp_sz, max_bytes);
                if (s_read(*data, out_buf, to_get, DATA_TIMEOUT * 2)) result = to_get;
            } else if (method == 8) {
                uint8_t *cbuf = (uint8_t *)heap_caps_malloc(comp_sz,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!cbuf) cbuf = (uint8_t *)malloc(comp_sz);
                if (!cbuf) { log_w("FTPS ZIP: OOM cbuf %u bytes", comp_sz); break; }
                if (s_read(*data, cbuf, comp_sz, DATA_TIMEOUT * 2)) {
                    unsigned long destlen = (unsigned long)max_bytes;
                    unsigned long srclen  = (unsigned long)comp_sz;
                    int ret = puff(out_buf, &destlen, cbuf, &srclen);
                    if (ret == 0) result = (size_t)destlen;
                    else log_w("FTPS inflate error %d [%s]", ret, fname);
                }
                free(cbuf);
            } else {
                log_w("FTPS ZIP: unsupported method %d for '%s'", method, fname);
            }
        }
        break;
    }

    data->stop();
    delete data;
    _drainCtrl();
    return result;
}

// ═════════════════════════════════════════════════════════════════════════════
// _listDir — CWD → PASV → LIST → TLS data (session resumed) → drain ctrl
// ═════════════════════════════════════════════════════════════════════════════
void FtpsClient::_listDir(const char *dir) {
    if (!_ctrl) return;

    char cwd_cmd[160];
    snprintf(cwd_cmd, sizeof(cwd_cmd), "CWD %s", dir);
    if (!_sendCmd(cwd_cmd, 250)) {
        log_w("FTPS CWD failed for listing: %s", dir);
        return;
    }

    for (int pass = 0; pass < 2; pass++) {
        char     pasv_reply[128] = {};
        if (!_sendCmd("PASV", 227, pasv_reply, sizeof(pasv_reply))) return;
        char     data_ip[20] = {};
        uint16_t data_port   = 0;
        if (!_parsePasv(pasv_reply, data_ip, data_port)) return;

        const char *list_cmd = (pass == 0) ? "LIST\r\n" : "NLST\r\n";
        _ctrl->print(list_cmd);
        _ctrl->flush();

        // TLS data channel — session resumed from ctrl (same logic as _openData)
        BambuTlsClient *data = new BambuTlsClient();
        bool conn_ok = data->connect(data_ip, data_port,
                                     _has_session ? &_session : nullptr,
                                     TIMEOUT_MS, /*use_psram=*/true);
        if (!conn_ok) {
            log_w("FTPS LIST data connect failed for %s", dir);
            delete data;
            _drainCtrl();
            return;
        }

        // Wait for 150 on ctrl
        char     reply[128] = {};
        uint32_t t          = millis();
        bool     started    = false;
        while (millis() - t < 5000) {
            if (_ctrl->available()) {
                _ctrl->readStringUntil('\n').toCharArray(reply, sizeof(reply));
                int code = atoi(reply);
                if (reply[3] == ' ' && code > 0) {
                    log_i("FTPS < %s", reply);
                    if (code == 150 || code == 125) { started = true; break; }
                    if (code >= 400) {
                        log_w("FTPS LIST rejected (%d) for %s", code, dir);
                        data->stop(); delete data;
                        return;
                    }
                }
            }
            delay(5);
        }
        if (!started) {
            log_w("FTPS LIST no 150 for %s", dir);
            data->stop(); delete data;
            _drainCtrl();
            return;
        }

        log_i("FTPS DIR %s (pass %d):", dir, pass);
        uint32_t dt = millis();
        String   line;
        int      count = 0;
        while ((data->connected() || data->available()) && millis() - dt < 5000) {
            if (data->available()) {
                char c = (char)data->read();
                if (c == '\n') {
                    line.trim();
                    if (line.length()) { log_i("  %s", line.c_str()); count++; }
                    line = "";
                    dt   = millis();
                } else {
                    line += c;
                }
            } else delay(1);
        }
        if (line.length()) { log_i("  %s", line.c_str()); count++; }
        data->stop();
        delete data;
        _drainCtrl();

        if (count > 0) break;
        if (pass == 0) log_i("FTPS LIST empty for %s, retrying with NLST", dir);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Public entry point
// ═════════════════════════════════════════════════════════════════════════════
size_t FtpsClient::downloadThumbnail(const char *job_name,
                                      const char *gcode_file,
                                      uint8_t    *out_buf,
                                      size_t      max_bytes) {
    log_i("FTPS thumb job='%s' gcode='%s'",
          job_name   ? job_name   : "(null)",
          gcode_file ? gcode_file : "(null)");
    if (!_ensureControl()) { log_w("FTPS ctrl connect failed"); return 0; }

    size_t got = 0;

    // ── 0. Primary: derive path from gcode_file hint ─────────────────────────
    if (gcode_file && strlen(gcode_file) > 6) {
        char base[128] = {};
        strncpy(base, gcode_file, sizeof(base) - 1);
        const char *gcode_exts[] = { ".gcode", ".g", nullptr };
        for (int i = 0; gcode_exts[i]; i++) {
            size_t blen = strlen(base), elen = strlen(gcode_exts[i]);
            if (blen > elen && strcmp(base + blen - elen, gcode_exts[i]) == 0) {
                base[blen - elen] = '\0';
                break;
            }
        }

        const char *img_exts[] = { ".png", ".jpg", nullptr };

        // Try A: full path as-is
        for (int i = 0; img_exts[i] && !got; i++) {
            char path[160];
            snprintf(path, sizeof(path), "%s%s", base, img_exts[i]);
            log_i("FTPS trying (full path): %s", path);
            if (_ensureControl())
                got = _downloadFile(path, out_buf, max_bytes);
        }

        // Try B: strip SD mount prefix ("/data/" or "/sdcard/")
        const char *ftp_base = base;
        if      (strncmp(base, "/data/",   6) == 0) ftp_base = base + 5;
        else if (strncmp(base, "/sdcard/", 8) == 0) ftp_base = base + 7;

        if (ftp_base != base) {
            for (int i = 0; img_exts[i] && !got; i++) {
                char path[160];
                snprintf(path, sizeof(path), "%s%s", ftp_base, img_exts[i]);
                log_i("FTPS trying (stripped): %s", path);
                if (_ensureControl())
                    got = _downloadFile(path, out_buf, max_bytes);
            }
        }
    }

    // ── 1. .3mf in / then /cache/ ────────────────────────────────────────────
    // Bambu stores files as "{name}.gcode.3mf" (not just "{name}.3mf"),
    // so try the .gcode.3mf variant first, then the bare .3mf as a fallback.
    if (!got) {
        const char *dirs[]    = { "/cache/", "/", nullptr };
        const char *mf_exts[] = { ".gcode.3mf", ".3mf", nullptr };
        for (int d = 0; dirs[d] && !got; d++) {
            for (int e = 0; mf_exts[e] && !got; e++) {
                char mf_path[160];
                snprintf(mf_path, sizeof(mf_path), "%s%s%s", dirs[d], job_name, mf_exts[e]);
                log_i("FTPS trying .3mf: %s", mf_path);
                if (_ensureControl())
                    got = _extractFromMf(mf_path, out_buf, max_bytes);
            }
        }
    }

    // ── 2. Flat image named after job in / and /cache/ ───────────────────────
    if (!got) {
        const char *dirs[] = { "/", "/cache/", nullptr };
        const char *exts[] = { ".jpg", ".png", ".jpeg", nullptr };
        for (int d = 0; dirs[d] && !got; d++) {
            for (int i = 0; exts[i] && !got; i++) {
                if (!_ensureControl()) break;
                char path[128];
                snprintf(path, sizeof(path), "%s%s%s", dirs[d], job_name, exts[i]);
                log_i("FTPS trying flat: %s", path);
                got = _downloadFile(path, out_buf, max_bytes);
            }
        }
    }

    // ── 3. Last-resort generic thumb names ───────────────────────────────────
    if (!got) {
        const char *generic[] = { "/thumb.jpg", "/cache/thumb.jpg", nullptr };
        for (int i = 0; generic[i] && !got; i++) {
            if (_ensureControl())
                got = _downloadFile(generic[i], out_buf, max_bytes);
        }
    }

    _disconnect();
    log_i("FTPS thumb result: %u bytes", got);
    return got;
}
