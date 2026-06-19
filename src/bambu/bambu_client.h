#pragma once
/**
 * bambu_client.h
 *
 * Handles the MQTT connection to a Bambu Lab printer
 * (X1C / X1E / P1S / P1P / A1 series) over local network.
 *
 * Protocol:
 *   Host:     <printer IP>
 *   Port:     8883  (TLS, certificate not verified)
 *   User:     bblp
 *   Password: <access code>
 *   Sub:      device/<serial>/report
 *   Pub:      device/<serial>/request
 */

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ── Print state ───────────────────────────────────────────────
enum class PrintState {
    UNKNOWN,
    IDLE,
    PREPARE,
    RUNNING,
    PAUSE,
    FINISH,
    FAILED
};

// ── Aggregated printer data ───────────────────────────────────
struct PrinterStatus {
    PrintState state        = PrintState::UNKNOWN;
    char       state_str[16]= {};       // raw string from MQTT
    char       job_name[64] = {};       // subtask_name
    char       project_id[32]= {};      // project_id (for FTP)
    char       gcode_file[128]= {};     // full on-printer path, e.g. /sdcard/cache/foo.3mf
    uint8_t    progress     = 0;        // 0-100 %
    uint16_t   remaining_min= 0;        // minutes
    uint16_t   layer_cur    = 0;
    uint16_t   layer_total  = 0;
    float      temp_nozzle  = 0.f;
    float      temp_nozzle_t= 0.f;     // target
    float      temp_bed     = 0.f;
    float      temp_bed_t   = 0.f;
    float      temp_chamber = 0.f;
    uint8_t    speed_pct    = 100;
    char       wifi_signal[12]= {};
    bool       sd_present   = false;
    bool       fresh_thumb   = false;   // true when job_name changed → fetch thumb
};

// ── Callback type ─────────────────────────────────────────────
typedef std::function<void(const PrinterStatus &)> StatusCallback;

// ── BambuClient class ─────────────────────────────────────────
class BambuClient {
public:
    BambuClient();

    void begin(const char *ip,
               const char *serial,
               const char *access_code);

    /** Call frequently from the main loop (non-blocking). */
    void loop();

    /** Returns true when connected to the printer MQTT broker. */
    bool isConnected();

    /** Register callback for parsed status updates. */
    void onStatus(StatusCallback cb) { _cb = cb; }

    /** Latest snapshot (always valid after first message). */
    const PrinterStatus &status() const { return _status; }

    /** Request a full status push from the printer. */
    void requestFullUpdate();

    /** Print control commands (no-op if not connected). */
    void pause();
    void resume();
    void stop();
    void sendCommand(const char *json);

private:
    void _connect();
    void _onMessage(const char *topic, uint8_t *payload, unsigned int len);
    void _parse(JsonDocument &doc);
    PrintState _stateFromString(const char *s);

    char _ip[24]     = {};
    char _serial[32] = {};
    char _code[16]   = {};
    char _sub_topic[64] = {};
    char _pub_topic[64] = {};

    WiFiClientSecure *_tls  = nullptr;  // recreated on every connect attempt
    PubSubClient      _mqtt;            // uses default ctor; setClient() before each connect
    StatusCallback   _cb;
    PrinterStatus    _status;

    unsigned long _last_connect_attempt = 0;
    char          _last_job_name[64]    = {};
    static constexpr unsigned long RECONNECT_INTERVAL = 30000; // 30s between attempts
};
