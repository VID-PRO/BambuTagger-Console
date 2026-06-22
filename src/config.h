#pragma once
/**
 * config.h — User-editable defaults
 *
 * These are the compile-time fallbacks.  All values can also be
 * changed at runtime via the on-screen Configuration page and are
 * then stored in NVS (Preferences).
 */

// -- Version info ----------------------------------------------
#define APP_NAME    "BambuTagger-Console"
#define APP_VERSION "1.3.0"

// ── WiFi ──────────────────────────────────────────────────────
#define DEFAULT_WIFI_SSID     "YourWiFiSSID"
#define DEFAULT_WIFI_PASSWORD "YourWiFiPassword"

// ── Printers ──────────────────────────────────────────────────
#define MAX_PRINTERS  4

struct PrinterConfig {
    char ip[24]     = {};
    char serial[32] = {};
    char code[16]   = {};
};

#define DEFAULT_BAMBU_IP      "192.168.1.100"
#define DEFAULT_BAMBU_SERIAL  "01S00C123456789"
#define DEFAULT_BAMBU_CODE    "12345678"

// ── MQTT (do not change unless you know what you're doing) ────
#define BAMBU_MQTT_PORT   8883   // TLS
#define BAMBU_MQTT_USER   "bblp"

// ── FTP thumbnail (implicit TLS, port 990) ────────────────────
#define BAMBU_FTP_PORT    990

// ── Thumbnail target size on display ─────────────────────────
#define THUMB_W  240
#define THUMB_H  240

// ── Display (ESP32-8048S043 / Sunton 800×480) ─────────────────
#define LCD_WIDTH   800
#define LCD_HEIGHT  480
#define LCD_BL_PIN  2

// ── Sidebar width ─────────────────────────────────────────────
#define SIDEBAR_W   80
