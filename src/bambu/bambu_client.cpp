#include "bambu_client.h"
#include <Arduino.h>

// URL-decode in-place: replaces %XX and + with the actual character
static void urlDecode(char *dst, const char *src, size_t dstLen) {
    size_t i = 0;
    while (*src && i + 1 < dstLen) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], 0 };
            dst[i++] = (char)strtol(hex, nullptr, 16);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

BambuClient::BambuClient() {}

// ─────────────────────────────────────────────────────────────
void BambuClient::begin(const char *ip, const char *serial, const char *access_code) {
    strncpy(_ip,     ip,           sizeof(_ip)     - 1);
    strncpy(_serial, serial,       sizeof(_serial) - 1);
    strncpy(_code,   access_code,  sizeof(_code)   - 1);
    strncpy(_status.ip, ip, sizeof(_status.ip) - 1);

    snprintf(_sub_topic, sizeof(_sub_topic), "device/%s/report",  _serial);
    snprintf(_pub_topic, sizeof(_pub_topic), "device/%s/request", _serial);

    _mqtt.setServer(_ip, 8883);
    _mqtt.setBufferSize(32768);  // Bambu pushall can exceed 15 KB with AMS data
    _mqtt.setCallback([this](char *topic, uint8_t *payload, unsigned int len) {
        _onMessage(topic, payload, len);
    });

    // log_i("BambuClient configured for %s @ %s", _serial, _ip);
}

// ─────────────────────────────────────────────────────────────
void BambuClient::loop() {
    if (!_mqtt.connected()) {
        unsigned long now = millis();
        if (now - _last_connect_attempt >= RECONNECT_INTERVAL) {
            _last_connect_attempt = now;
            _connect();
        }
        return;
    }
    _mqtt.loop();
}

// ─────────────────────────────────────────────────────────────
bool BambuClient::isConnected() { return _mqtt.connected(); }

// ─────────────────────────────────────────────────────────────
void BambuClient::requestFullUpdate() {
    if (!_mqtt.connected()) return;
    // Bambu "get_version" + "pushing_all" request
    static const char *req =
        "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}";
    _mqtt.publish(_pub_topic, req);
}

// ─────────────────────────────────────────────────────────────
void BambuClient::sendCommand(const char *json) {
    if (!_mqtt.connected()) return;
    _mqtt.publish(_pub_topic, json);
}

void BambuClient::pause() {
    sendCommand("{\"print\":{\"sequence_id\":\"0\",\"command\":\"pause\"}}");
}

void BambuClient::resume() {
    sendCommand("{\"print\":{\"sequence_id\":\"0\",\"command\":\"resume\"}}");
}

void BambuClient::stop() {
    sendCommand("{\"print\":{\"sequence_id\":\"0\",\"command\":\"stop\"}}");
}

// ─────────────────────────────────────────────────────────────
void BambuClient::_connect() {
    // log_i("MQTT connecting to %s …", _ip);

    // Delete and recreate the TLS client on every attempt.
    // Calling stop() on a WiFiClientSecure value-member leaves the socket fd
    // in an invalid state (errno 9) — a fresh heap object avoids that entirely.
    if (_tls) { _tls->stop(); delete _tls; }
    _tls = new WiFiClientSecure();
    _tls->setInsecure();     // Bambu uses a self-signed certificate
    _tls->setTimeout(10);    // seconds
    _mqtt.setClient(*_tls);

    // Unique client ID
    char cid[24];
    snprintf(cid, sizeof(cid), "bambu_esp_%06lX", (unsigned long)(ESP.getEfuseMac() & 0xFFFFFF));

    if (_mqtt.connect(cid, "bblp", _code)) {
        // log_i("MQTT connected");
        _mqtt.subscribe(_sub_topic);
        requestFullUpdate();
    } else {
        log_w("MQTT connect failed, rc=%d", _mqtt.state());
    }
}

// ─────────────────────────────────────────────────────────────
void BambuClient::_onMessage(const char *topic, uint8_t *payload, unsigned int len) {
    // log_i("MQTT msg: topic=%s len=%u", topic, len);

    // Parse JSON
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) {
        log_w("JSON parse error: %s (len=%u)", err.c_str(), len);
        return;
    }
    _parse(doc);
}

// ─────────────────────────────────────────────────────────────
void BambuClient::_parse(JsonDocument &doc) {
    JsonObject print = doc["print"];
    if (print.isNull()) return;  // Could be "system" or "info" message

    const char *cmd = print["command"] | "";
    // log_d("print command: \"%s\"", cmd);
    // Accept push_status, pushall, project_file, and empty-command messages.
    // Skip only unknown non-empty commands (e.g. "ams_filament_setting").
    if (strlen(cmd) > 0
        && strcmp(cmd, "push_status")   != 0
        && strcmp(cmd, "pushall")       != 0
        && strcmp(cmd, "project_file")  != 0
        && strcmp(cmd, "gcode_line")    != 0) {
        // log_d("skipping command: %s", cmd);
        return;
    }

    // ── State ────────────────────────────────────────────────
    const char *gs = print["gcode_state"] | "";
    if (strlen(gs) > 0) {
        strncpy(_status.state_str, gs, sizeof(_status.state_str) - 1);
        _status.state = _stateFromString(gs);
    }

    // ── Job info ─────────────────────────────────────────────
    const char *jn = print["subtask_name"] | "";
    if (strlen(jn) > 0 && strcmp(jn, _last_job_name) != 0) {
        urlDecode(_status.job_name, jn, sizeof(_status.job_name));
        strncpy(_last_job_name, jn, sizeof(_last_job_name) - 1);
        _status.fresh_thumb = true;  // signal: fetch new thumbnail
    }
    const char *pid = print["project_id"] | "";
    if (strlen(pid) > 0) strncpy(_status.project_id, pid, sizeof(_status.project_id) - 1);

    // gcode_file gives the actual on-printer path, e.g. /sdcard/cache/foo.3mf
    const char *gf = print["gcode_file"] | "";
    if (strlen(gf) > 0) {
        strncpy(_status.gcode_file, gf, sizeof(_status.gcode_file) - 1);
        // log_i("gcode_file: %s", gf);
    }

    // ── Progress ─────────────────────────────────────────────
    if (!print["mc_percent"].isNull())
        _status.progress = (uint8_t)print["mc_percent"].as<int>();

    if (!print["mc_remaining_time"].isNull())
        _status.remaining_min = (uint16_t)print["mc_remaining_time"].as<int>();

    if (!print["layer_num"].isNull())
        _status.layer_cur   = (uint16_t)print["layer_num"].as<int>();

    if (!print["total_layer_num"].isNull())
        _status.layer_total = (uint16_t)print["total_layer_num"].as<int>();

    // ── Temperatures ─────────────────────────────────────────
    if (!print["nozzle_temper"].isNull())
        _status.temp_nozzle   = print["nozzle_temper"].as<float>();

    if (!print["nozzle_target_temper"].isNull())
        _status.temp_nozzle_t = print["nozzle_target_temper"].as<float>();

    if (!print["bed_temper"].isNull())
        _status.temp_bed      = print["bed_temper"].as<float>();

    if (!print["bed_target_temper"].isNull())
        _status.temp_bed_t    = print["bed_target_temper"].as<float>();

    if (!print["chamber_temper"].isNull())
        _status.temp_chamber  = print["chamber_temper"].as<float>();

    // ── Misc ─────────────────────────────────────────────────
    if (!print["spd_mag"].isNull())
        _status.speed_pct = (uint8_t)print["spd_mag"].as<int>();

    const char *sig = print["wifi_signal"] | "";
    if (strlen(sig) > 0) strncpy(_status.wifi_signal, sig, sizeof(_status.wifi_signal) - 1);

    _status.sd_present = print["sdcard"] | false;

    // Fire callback
    if (_cb) _cb(_status);
}

// ─────────────────────────────────────────────────────────────
PrintState BambuClient::_stateFromString(const char *s) {
    if (strcmp(s, "IDLE")    == 0) return PrintState::IDLE;
    if (strcmp(s, "RUNNING") == 0) return PrintState::RUNNING;
    if (strcmp(s, "PAUSE")   == 0) return PrintState::PAUSE;
    if (strcmp(s, "FINISH")  == 0) return PrintState::FINISH;
    if (strcmp(s, "FAILED")  == 0) return PrintState::FAILED;
    if (strcmp(s, "PREPARE") == 0) return PrintState::PREPARE;
    return PrintState::UNKNOWN;
}
