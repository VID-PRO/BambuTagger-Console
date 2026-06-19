#include "ftps_client.h"
#include "config.h"
#include <WiFiClientSecure.h>
extern "C" {
#include "puff/puff.h"
}

// ═════════════════════════════════════════════════════════════
// Stream helpers — operate on a connected WiFiClientSecure
// ═════════════════════════════════════════════════════════════

static bool s_read(WiFiClientSecure &c, uint8_t *buf, size_t n, uint32_t tms) {
    size_t got = 0;
    uint32_t t = millis();
    while (got < n) {
        if (millis() - t > tms) return false;
        if (c.available()) {
            int r = c.read(buf + got, n - got);
            if (r > 0) { got += r; t = millis(); }
        } else delay(1);
    }
    return true;
}

static bool s_skip(WiFiClientSecure &c, uint32_t n, uint32_t tms) {
    uint8_t tmp[256];
    uint32_t done = 0;
    uint32_t t = millis();
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

// ═════════════════════════════════════════════════════════════
// Control-channel helpers
// ═════════════════════════════════════════════════════════════

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
    _ctrl = new WiFiClientSecure();
    _ctrl->setInsecure();
    _ctrl->setTimeout(TIMEOUT_MS / 1000);

    log_i("FTPS connecting to %s:%d", _ip, BAMBU_FTP_PORT);
    if (!_ctrl->connect(_ip, BAMBU_FTP_PORT)) {
        log_w("FTPS TCP/TLS connect failed");
        _freeCtrl();
        return false;
    }
    log_i("FTPS TLS handshake OK");

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
    _sendCmd("PROT P", 200);
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

// ═════════════════════════════════════════════════════════════
// _drainCtrl — consume any pending ctrl replies after a transfer.
// Call after closing the data channel to eat the 226 / error reply.
// ═════════════════════════════════════════════════════════════
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
                if (code == 226 || code >= 400) return;  // terminal reply consumed
            }
            t = millis();  // reset timeout on any data
        } else delay(5);
    }
}

// ═════════════════════════════════════════════════════════════
// _openData — CWD + PASV + RETR, then open TLS data channel.
//
// KEY INSIGHT: Do NOT free the ctrl TLS before connecting data.
// The Bambu server interprets ctrl close as an abort and sends
// nothing on the data channel — causing empty transfers.
//
// Flow:
//   1. CWD to directory
//   2. PASV → get data ip:port
//   3. Send RETR <filename> on ctrl (don't wait for reply yet)
//   4. Connect data TLS (ctrl stays alive)
//   5. Wait for 150/125 on ctrl (server sends it after data connects)
//   6. On 4xx/5xx → close data, return nullptr; ctrl stays for next cmd
//   7. On 150 → return data; caller must _drainCtrl() after transfer
// ═════════════════════════════════════════════════════════════
WiFiClientSecure *FtpsClient::_openData(const char *retr_path) {
    // Split retr_path into directory and filename
    char dir[128] = "/";
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

    // CWD to the directory
    char cwd_cmd[160];
    snprintf(cwd_cmd, sizeof(cwd_cmd), "CWD %s", dir);
    if (!_sendCmd(cwd_cmd, 250)) {
        log_w("FTPS CWD failed: %s", dir);
        return nullptr;  // ctrl stays alive for next attempt
    }

    char pasv_reply[128] = {};
    if (!_sendCmd("PASV", 227, pasv_reply, sizeof(pasv_reply))) return nullptr;

    char data_ip[20] = {};
    uint16_t data_port = 0;
    if (!_parsePasv(pasv_reply, data_ip, data_port)) return nullptr;

    // Send RETR — do NOT read the reply yet
    char cmd[192];
    snprintf(cmd, sizeof(cmd), "RETR %s", fname);
    _ctrl->print(String(cmd) + "\r\n");
    _ctrl->flush();

    // Connect data TLS — KEEP ctrl alive (closing ctrl aborts the transfer)
    WiFiClientSecure *data = new WiFiClientSecure();
    data->setInsecure();
    data->setTimeout(TIMEOUT_MS / 1000);
    if (!data->connect(data_ip, data_port)) {
        log_w("FTPS data TLS connect failed for %s", retr_path);
        delete data;
        _drainCtrl();  // consume any error reply on ctrl
        return nullptr;
    }

    // Now wait for 150/125 on ctrl (server sends it after data connects)
    // A 4xx/5xx here means the file doesn't exist.
    char reply[128] = {};
    uint32_t t = millis();
    bool started = false;
    while (millis() - t < 5000) {
        if (_ctrl->available()) {
            _ctrl->readStringUntil('\n').toCharArray(reply, sizeof(reply));
            int code = atoi(reply);
            if (reply[3] == ' ' && code > 0) {
                log_i("FTPS < %s", reply);
                if (code == 150 || code == 125) { started = true; break; }
                if (code >= 400) {
                    log_w("FTPS RETR rejected (%d): %s", code, retr_path);
                    data->stop(); delete data;
                    return nullptr;  // ctrl ready for next command
                }
            }
        }
        delay(5);
    }
    if (!started) {
        log_w("FTPS no 150 for %s — timeout, data closed", retr_path);
        data->stop(); delete data;
        _drainCtrl();
        return nullptr;
    }

    log_i("FTPS data TLS OK → %s", retr_path);
    return data;  // caller must _drainCtrl() after reading all data
}

// ═════════════════════════════════════════════════════════════
// Download a flat (non-ZIP) file into out_buf.
// ═════════════════════════════════════════════════════════════
size_t FtpsClient::_downloadFile(const char *path, uint8_t *buf, size_t max_bytes) {
    WiFiClientSecure *data = _openData(path);
    if (!data) return 0;

    size_t total = 0;
    uint32_t t = millis();
    const uint32_t RX_TIMEOUT = TIMEOUT_MS * 4;
    while ((data->connected() || data->available()) &&
           total < max_bytes &&
           millis() - t < RX_TIMEOUT) {
        if (data->available()) {
            int r = data->read(buf + total, max_bytes - total);
            if (r > 0) { total += r; t = millis(); }
        } else delay(1);
    }
    data->stop();
    delete data;
    _drainCtrl();  // consume 226 Transfer complete
    return total;
}

// ═════════════════════════════════════════════════════════════
// Stream a .3mf (ZIP) file from the printer and extract
// the thumbnail PNG embedded at Metadata/thumbnail.png
// (or Metadata/plate_1.png as fallback).
//
// ZIP local file header layout (after the 4-byte PK\x03\x04 sig):
//  Offset  Size  Field
//   0       2    version needed
//   2       2    general purpose bit flag
//   4       2    compression method  (0=stored, 8=deflate)
//   6       2    last-mod time
//   8       2    last-mod date
//  10       4    CRC-32
//  14       4    compressed size
//  18       4    uncompressed size
//  22       2    file-name length
//  24       2    extra-field length
// ═════════════════════════════════════════════════════════════
size_t FtpsClient::_extractFromMf(const char *path, uint8_t *out_buf, size_t max_bytes) {
    WiFiClientSecure *data = _openData(path);
    if (!data) return 0;

    const uint32_t MAX_SCAN    = 3UL * 1024 * 1024;
    const uint32_t DATA_TIMEOUT = TIMEOUT_MS * 6;
    uint32_t scanned = 0;
    size_t result = 0;

    while (scanned < MAX_SCAN) {
        uint8_t sig[4];
        if (!s_read(*data, sig, 4, DATA_TIMEOUT)) { log_w("FTPS ZIP: timeout @ sig"); break; }
        scanned += 4;

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
        uint32_t comp_sz   = hdr[14] | (uint32_t(hdr[15]) << 8) | (uint32_t(hdr[16]) << 16) | (uint32_t(hdr[17]) << 24);
        uint16_t fname_len = hdr[22] | (uint16_t(hdr[23]) << 8);
        uint16_t extra_len = hdr[24] | (uint16_t(hdr[25]) << 8);

        char fname[256] = {};
        uint16_t to_read = (fname_len < 255) ? fname_len : 255;
        if (!s_read(*data, (uint8_t*)fname, to_read, DATA_TIMEOUT)) break;
        scanned += fname_len;
        if (fname_len > to_read && !s_skip(*data, fname_len - to_read, DATA_TIMEOUT)) break;

        if (!s_skip(*data, extra_len, DATA_TIMEOUT)) break;
        scanned += extra_len;

        bool is_thumb = (strncmp(fname, "Metadata/thumbnail", 18) == 0 ||
                         strncmp(fname, "Metadata/plate_1",   16) == 0);

        if (!is_thumb) {
            if (comp_sz == 0 && (flags & 0x08)) {
                log_w("FTPS ZIP: streaming entry without size, cannot skip");
                break;
            }
            if (!s_skip(*data, comp_sz, DATA_TIMEOUT)) break;
            scanned += comp_sz;
            if (flags & 0x08) {
                s_skip(*data, 16, DATA_TIMEOUT);
                scanned += 16;
            }
            continue;
        }

        if (comp_sz == 0 || comp_sz > 1024UL * 1024) {
            log_w("FTPS ZIP: thumbnail comp_sz=%u out of range", comp_sz);
            break;
        }

        if (method == 0) {
            size_t to_get = min((size_t)comp_sz, max_bytes);
            if (s_read(*data, out_buf, to_get, DATA_TIMEOUT * 2)) {
                result = to_get;
            }
        } else if (method == 8) {
            uint8_t *cbuf = (uint8_t*)heap_caps_malloc(comp_sz,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!cbuf) cbuf = (uint8_t*)malloc(comp_sz);
            if (!cbuf) { log_w("FTPS ZIP: OOM for cbuf %u bytes", comp_sz); break; }

            if (s_read(*data, cbuf, comp_sz, DATA_TIMEOUT * 2)) {
                unsigned long destlen = (unsigned long)max_bytes;
                unsigned long srclen  = (unsigned long)comp_sz;
                int ret = puff(out_buf, &destlen, cbuf, &srclen);
                if (ret == 0) {
                    result = (size_t)destlen;
                } else {
                    log_w("FTPS inflate error %d [%s]", ret, fname);
                    result = 0;
                }
            }
            free(cbuf);
        } else {
            log_w("FTPS ZIP: unsupported method %d for thumbnail", method);
        }
        break;
    }

    data->stop();
    delete data;
    _drainCtrl();  // consume 226
    return result;
}

// ═════════════════════════════════════════════════════════════
// _listDir — CWD → PASV → LIST → data TLS (ctrl stays alive)
//            → wait for 150 on ctrl → read listing → drain 226
//
// Also tries NLST if LIST returns nothing (some servers differ).
// ═════════════════════════════════════════════════════════════
void FtpsClient::_listDir(const char *dir) {
    if (!_ctrl) return;

    // CWD to directory first
    char cwd_cmd[160];
    snprintf(cwd_cmd, sizeof(cwd_cmd), "CWD %s", dir);
    if (!_sendCmd(cwd_cmd, 250)) {
        log_w("FTPS CWD failed for listing: %s", dir);
        return;  // ctrl stays alive
    }

    for (int pass = 0; pass < 2; pass++) {
        // PASV
        char pasv_reply[128] = {};
        if (!_sendCmd("PASV", 227, pasv_reply, sizeof(pasv_reply))) return;
        char data_ip[20] = {};
        uint16_t data_port = 0;
        if (!_parsePasv(pasv_reply, data_ip, data_port)) return;

        // Send LIST (pass 0) or NLST (pass 1) — do NOT read reply yet
        const char *list_cmd = (pass == 0) ? "LIST\r\n" : "NLST\r\n";
        _ctrl->print(list_cmd);
        _ctrl->flush();

        // Connect data TLS — ctrl stays alive
        WiFiClientSecure *data = new WiFiClientSecure();
        data->setInsecure();
        data->setTimeout(TIMEOUT_MS / 1000);
        if (!data->connect(data_ip, data_port)) {
            log_w("FTPS LIST data connect failed for %s", dir);
            delete data;
            _drainCtrl();
            return;
        }

        // Wait for 150 on ctrl
        char reply[128] = {};
        uint32_t t = millis();
        bool started = false;
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

        // Read listing
        log_i("FTPS DIR %s (pass %d):", dir, pass);
        uint32_t dt = millis();
        String line;
        int count = 0;
        while ((data->connected() || data->available()) && millis() - dt < 5000) {
            if (data->available()) {
                char c = data->read();
                if (c == '\n') {
                    line.trim();
                    if (line.length()) { log_i("  %s", line.c_str()); count++; }
                    line = "";
                    dt = millis();
                } else {
                    line += c;
                }
            } else delay(1);
        }
        if (line.length()) { log_i("  %s", line.c_str()); count++; }
        data->stop();
        delete data;
        _drainCtrl();

        if (count > 0) break;  // got results, no need for NLST pass
        if (pass == 0) log_i("FTPS LIST empty for %s, retrying with NLST", dir);
    }
}

// ═════════════════════════════════════════════════════════════
// Public entry point
// ═════════════════════════════════════════════════════════════
size_t FtpsClient::downloadThumbnail(const char *job_name,
                                      const char *gcode_file,
                                      uint8_t    *out_buf,
                                      size_t      max_bytes) {
    if (!_ensureControl()) { log_w("FTPS ctrl connect failed"); return 0; }

    // ── DIAGNOSTIC: list dirs to map the FTP filesystem ──────────────
    log_i("FTPS SCAN job='%s' gcode='%s'", job_name ? job_name : "(null)", gcode_file ? gcode_file : "(null)");
    _listDir("/");
    if (!_ensureControl()) return 0;
    _listDir("/data");
    if (!_ensureControl()) return 0;
    _listDir("/data/Metadata");
    if (!_ensureControl()) return 0;
    _listDir("/Metadata");
    if (!_ensureControl()) return 0;
    _listDir("/cache");
    if (!_ensureControl()) return 0;
    // ── end diagnostic ───────────────────────────────────────────────

    size_t got = 0;

    // ── 0. Primary: derive path from gcode_file hint ──────────────────
    //   gcode_file from MQTT = "/data/Metadata/plate_1.gcode"
    //   FTP server root appears to be different from internal fs mount.
    //   Try A: path as-is          → /data/Metadata/plate_1.png
    //   Try B: strip /data prefix  → /Metadata/plate_1.png
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

        // Try A: full path
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

    // ── 1. .3mf in / then /cache/ ─────────────────────────────────────
    if (!got) {
        const char *dirs[] = { "/", "/cache/", nullptr };
        for (int d = 0; dirs[d] && !got; d++) {
            char mf_path[128];
            snprintf(mf_path, sizeof(mf_path), "%s%s.3mf", dirs[d], job_name);
            log_i("FTPS trying .3mf: %s", mf_path);
            if (_ensureControl())
                got = _extractFromMf(mf_path, out_buf, max_bytes);
        }
    }

    // ── 2. Flat image named after job in / and /cache/ ────────────────
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

    // ── 3. Last-resort generic thumb names ───────────────────────────
    if (!got) {
        const char *generic[] = { "/thumb.jpg", "/cache/thumb.jpg", nullptr };
        for (int i = 0; generic[i] && !got; i++) {
            if (_ensureControl())
                got = _downloadFile(generic[i], out_buf, max_bytes);
        }
    }

    _disconnect();
    return got;
}
