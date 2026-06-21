/**
 * main.cpp — BambuTagger-Console for ESP32-8048S043
 *
 * Architecture:
 *   Core 0 (Arduino loop):  LVGL tick + render
 *   Core 1 (FreeRTOS task): WiFi, MQTT, FTP thumbnail fetch
 *
 * Task flow:
 *   1. Boot → display init → show splash
 *   2. Load credentials from NVS (fallback to config.h defaults)
 *   3. Connect WiFi → connect Bambu MQTT
 *   4. On each status push: update UI
 *   5. On new job detected: fetch thumbnail via FTPS (separate task)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "config.h"
#include "display/display_driver.h"
#include "display/touch_calibration.h"
#include "wifi_portal.h"
#include "bambu/bambu_client.h"
#include "bambu/ftps_client.h"
#include "ui/ui_manager.h"
#include "ota_update.h"

// ── Global LCD instance (declared extern in display_driver.h) ─
LGFX lcd;

// Flag set by config screen to trigger calibration from loop()
static volatile bool g_run_calibration = false;

// True when running in AP/portal mode (no network task, no UI)
static bool g_portal_mode = false;

// ── Global objects ────────────────────────────────────────────
static UIManager      g_ui;
static BambuClient    g_bambu[MAX_PRINTERS];
static FtpsClient     g_ftp[MAX_PRINTERS];

// Thumbnail state (written by FTP task, read by LVGL task)
static SemaphoreHandle_t g_thumb_mutex = nullptr;
static uint8_t           *g_thumb_buf  = nullptr;   // raw PNG bytes in PSRAM
static size_t             g_thumb_sz   = 0;
static lv_img_dsc_t       g_thumb_dsc  = {};
static uint8_t           *g_thumb_rgb  = nullptr;   // decoded RGB565 in PSRAM
static bool               g_thumb_ready= false;     // new frame available
static int                g_thumb_ready_pi = -1;    // printer index for ready frame

// Credentials (loaded from NVS)
static char g_wifi_ssid[64]  = DEFAULT_WIFI_SSID;
static char g_wifi_pass[64]  = DEFAULT_WIFI_PASSWORD;
static PrinterConfig g_printer_cfg[MAX_PRINTERS];
static int g_printer_count   = 0;

// ── LVGL mutex (guards lv_* calls from multiple cores) ────────
SemaphoreHandle_t g_lv_mutex = nullptr;

#define LV_LOCK()   xSemaphoreTake(g_lv_mutex, portMAX_DELAY)
#define LV_UNLOCK() xSemaphoreGive(g_lv_mutex)

// ─────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────
static void loadCredentials() {
    Preferences prefs;
    prefs.begin("bambu_mon", true);

    if (prefs.isKey("wifi_ssid")) {
        prefs.getString("wifi_ssid",  g_wifi_ssid,  sizeof(g_wifi_ssid));
        prefs.getString("wifi_pass",  g_wifi_pass,  sizeof(g_wifi_pass));
    }

    g_printer_count = prefs.getInt("bam_count", 0);
    if (g_printer_count == 0) {
        if (prefs.isKey("bam_ip")) {
            prefs.getString("bam_ip",     g_printer_cfg[0].ip,     sizeof(g_printer_cfg[0].ip));
            prefs.getString("bam_serial", g_printer_cfg[0].serial, sizeof(g_printer_cfg[0].serial));
            prefs.getString("bam_code",   g_printer_cfg[0].code,   sizeof(g_printer_cfg[0].code));
            g_printer_count = 1;
        }
    } else {
        for (int i = 0; i < g_printer_count && i < MAX_PRINTERS; i++) {
            char key[16];
            snprintf(key, sizeof(key), "bam_ip_%d", i);
            prefs.getString(key, g_printer_cfg[i].ip, sizeof(g_printer_cfg[i].ip));
            snprintf(key, sizeof(key), "bam_serial_%d", i);
            prefs.getString(key, g_printer_cfg[i].serial, sizeof(g_printer_cfg[i].serial));
            snprintf(key, sizeof(key), "bam_code_%d", i);
            prefs.getString(key, g_printer_cfg[i].code, sizeof(g_printer_cfg[i].code));
        }
    }
    prefs.end();

    if (g_printer_count == 0) {
        strncpy(g_printer_cfg[0].ip,     DEFAULT_BAMBU_IP,     sizeof(g_printer_cfg[0].ip) - 1);
        strncpy(g_printer_cfg[0].serial, DEFAULT_BAMBU_SERIAL, sizeof(g_printer_cfg[0].serial) - 1);
        strncpy(g_printer_cfg[0].code,   DEFAULT_BAMBU_CODE,   sizeof(g_printer_cfg[0].code) - 1);
        g_printer_count = 1;
    }
}

static void connectWifi() {
    log_i("Connecting to WiFi: %s", g_wifi_ssid);
    LV_LOCK(); g_ui.setConnecting(true); LV_UNLOCK();  // orange = connecting
    WiFi.setHostname("BambuTagger-Console");
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_wifi_ssid, g_wifi_pass);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
        delay(500);
    }
    if (WiFi.status() == WL_CONNECTED) {
        log_i("WiFi connected: %s", WiFi.localIP().toString().c_str());
        wifi_sta_server_start();  // web config UI reachable at device IP
        LV_LOCK(); g_ui.setConnecting(false); LV_UNLOCK(); // green = WiFi up
    } else {
        log_w("WiFi connect timeout");
        // stays orange — will retry in loop
    }
}

static void reconnectAll() {
    if (WiFi.status() != WL_CONNECTED) connectWifi();
    for (int i = 0; i < g_printer_count; i++) {
        g_bambu[i].begin(g_printer_cfg[i].ip, g_printer_cfg[i].serial, g_printer_cfg[i].code);
        g_ftp[i].begin(g_printer_cfg[i].ip, g_printer_cfg[i].code);
    }
}

// ─────────────────────────────────────────────────────────────
// Thumbnail fetch task (runs on Core 1)
// ─────────────────────────────────────────────────────────────
struct ThumbRequest {
    int   printer;
    char  job[64];
    char  gcode[128];
};

static QueueHandle_t    g_thumb_queue          = nullptr;
static TaskHandle_t     g_thumb_task_handle    = nullptr;

static void thumbTask(void *param) {
    ThumbRequest req;
    for (;;) {
        if (xQueueReceive(g_thumb_queue, &req, portMAX_DELAY) != pdTRUE) continue;

        log_i("Fetching thumbnail for printer %d: %s", req.printer, req.job);

        if (!g_thumb_buf) {
            g_thumb_buf = (uint8_t *)ps_malloc(256 * 1024);
            if (!g_thumb_buf) { log_e("ps_malloc thumb_buf failed"); continue; }
        }

        size_t png_sz = g_ftp[req.printer].downloadThumbnail(req.job, req.gcode, g_thumb_buf, 256*1024);
        if (png_sz == 0) {
            log_w("Thumbnail download failed, showing placeholder");
            LV_LOCK();
            g_ui.statusScreen(req.printer).clearThumbnail();
            LV_UNLOCK();
            continue;
        }
        if (!g_thumb_rgb) {
            g_thumb_rgb = (uint8_t *)ps_malloc(THUMB_W * THUMB_H * 2);
            if (!g_thumb_rgb) { log_e("ps_malloc thumb_rgb failed"); continue; }
        }

        lgfx::LGFX_Sprite sprite(&lcd);
        sprite.setColorDepth(16);
        sprite.setPsram(true);
        if (sprite.createSprite(THUMB_W, THUMB_H)) {
            float scale = 1.0f;
            if (png_sz >= 24) {
                uint32_t png_w = ((uint32_t)g_thumb_buf[16]<<24)|((uint32_t)g_thumb_buf[17]<<16)
                                |((uint32_t)g_thumb_buf[18]<<8)|g_thumb_buf[19];
                uint32_t png_h = ((uint32_t)g_thumb_buf[20]<<24)|((uint32_t)g_thumb_buf[21]<<16)
                                |((uint32_t)g_thumb_buf[22]<<8)|g_thumb_buf[23];
                if (png_w > 0 && png_h > 0) {
                    float sx = (float)THUMB_W / png_w;
                    float sy = (float)THUMB_H / png_h;
                    scale = (sx < sy) ? sx : sy;
                }
            }
            sprite.drawPng(g_thumb_buf, png_sz, 0, 0, THUMB_W, THUMB_H,
                           0, 0, scale, scale, lgfx::datum_t::middle_center);
            sprite.readRect(0, 0, THUMB_W, THUMB_H, (lgfx::rgb565_t *)g_thumb_rgb);
            sprite.deleteSprite();

            g_thumb_dsc.header.always_zero = 0;
            g_thumb_dsc.header.w           = THUMB_W;
            g_thumb_dsc.header.h           = THUMB_H;
            g_thumb_dsc.header.cf          = LV_IMG_CF_TRUE_COLOR;
            g_thumb_dsc.data_size          = THUMB_W * THUMB_H * 2;
            g_thumb_dsc.data               = g_thumb_rgb;

            xSemaphoreTake(g_thumb_mutex, portMAX_DELAY);
            g_thumb_ready = true;
            g_thumb_ready_pi = req.printer;
            xSemaphoreGive(g_thumb_mutex);
        } else {
            log_e("Sprite alloc failed for thumbnail decode");
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Network / MQTT task (Core 1)
// ─────────────────────────────────────────────────────────────
static void networkTask(void *param) {
    loadCredentials();
    connectWifi();

    // Configure per-printer status callbacks & start connections
    for (int i = 0; i < g_printer_count; i++) {
        g_bambu[i].onStatus([i](const PrinterStatus &s) {
            // Fill in printer index for the UI
            PrinterStatus sc = s;
            sc.printer_idx = i;
            {
                const char *ser = g_bambu[i].getSerial();
                size_t slen = strlen(ser);
                snprintf(sc.name, sizeof(sc.name), "3DP-%.3s-%.3s",
                         ser, slen >= 3 ? ser + slen - 3 : ser);
            }
            LV_LOCK();
            g_ui.updateStatus(i, sc);
            LV_UNLOCK();

            if (s.fresh_thumb && strlen(s.job_name) > 0) {
                log_i("MQTT thumb trigger printer=%d job=%s", i, s.job_name);
                if (g_thumb_queue) {
                    ThumbRequest req;
                    req.printer = i;
                    strncpy(req.job,   s.job_name,   sizeof(req.job)   - 1);
                    strncpy(req.gcode, s.gcode_file, sizeof(req.gcode) - 1);
                    BaseType_t qok = xQueueSend(g_thumb_queue, &req, 0);
                    if (qok != pdTRUE) log_w("thumb queue FULL, dropping request");
                }
                ((PrinterStatus &)s).fresh_thumb = false;
            }
        });
        g_bambu[i].begin(g_printer_cfg[i].ip, g_printer_cfg[i].serial, g_printer_cfg[i].code);
        g_ftp[i].begin(g_printer_cfg[i].ip, g_printer_cfg[i].code);
        {
            char name[32];
            snprintf(name, sizeof(name), "Printer %d", i + 1);
            LV_LOCK();
            g_ui.statusScreen(i).setPrinterName(name);
            LV_UNLOCK();
        }
    }

    LV_LOCK();
    g_ui.configWifiScreen().loadValues(g_wifi_ssid, g_wifi_pass);
    g_ui.configPrinterScreen().loadValues(g_printer_cfg, g_printer_count);
    g_ui.setNumPrinters(g_printer_count);
    LV_UNLOCK();

    bool last_wifi_ok = false;
    bool last_mqtt_ok = false;

    for (;;) {
        if (WiFi.status() != WL_CONNECTED) connectWifi();

        bool any_mqtt = false;
        for (int i = 0; i < g_printer_count; i++) {
            g_bambu[i].loop();
            if (g_bambu[i].isConnected()) any_mqtt = true;
        }

        bool wifi_ok = (WiFi.status() == WL_CONNECTED);
        if (wifi_ok != last_wifi_ok || any_mqtt != last_mqtt_ok) {
            LV_LOCK();
            g_ui.setConnecting(!wifi_ok);
            g_ui.setMqttConnected(any_mqtt);
            LV_UNLOCK();
            last_wifi_ok = wifi_ok;
            last_mqtt_ok = any_mqtt;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ─────────────────────────────────────────────────────────────
// Arduino entry points
// ─────────────────────────────────────────────────────────────
// ── Portal mode: LVGL screen shown while AP is active ────────
static void _show_portal_screen() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    // Icon
    lv_obj_t *icon = lv_label_create(scr);
    lv_label_set_text(icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x1DB954), 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -80);

    // Title
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Setup Mode");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x1DB954), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);

    // Instructions card
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, 480, 160);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 50);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x16213E), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x0F3460), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l1 = lv_label_create(card);
    lv_label_set_text(l1, "1.  Connect to WiFi:  " LV_SYMBOL_WIFI "  BambuTagger-Console");
    lv_obj_set_style_text_font(l1, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(l1, lv_color_hex(0xEAEAEA), 0);
    lv_obj_align(l1, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *l2 = lv_label_create(card);
    lv_label_set_text(l2, "2.  Open browser:  http://192.168.4.1");
    lv_obj_set_style_text_font(l2, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(l2, lv_color_hex(0xEAEAEA), 0);
    lv_obj_align(l2, LV_ALIGN_TOP_LEFT, 0, 36);

    lv_obj_t *l3 = lv_label_create(card);
    lv_label_set_text(l3, "3.  Enter WiFi + printer details and tap Save.");
    lv_obj_set_style_text_font(l3, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l3, lv_color_hex(0x8899AA), 0);
    lv_obj_align(l3, LV_ALIGN_TOP_LEFT, 0, 76);

    lv_scr_load(scr);
}

void setup() {
    Serial.begin(115200);
    log_i("BambuTagger-Console booting…");

    // Initialise display + LVGL
    display_init();

    // Load touch calibration from NVS (applies immediately to s_tcal).
    touch_cal_load();

    // First-boot calibration wizard: runs if no valid NVS data exists.
    // Uses LGFX direct draw; LVGL tasks have not started yet — safe to block.
    if (!touch_cal_has()) {
        log_i("No touch calibration found — running first-boot wizard");
        touch_cal_run(lcd);
    }

    // Mutexes
    g_lv_mutex    = xSemaphoreCreateMutex();
    g_thumb_mutex = xSemaphoreCreateMutex();

    // ── Check for saved WiFi credentials ─────────────────────
    if (!wifi_portal_has_credentials()) {
        log_i("No WiFi credentials found — starting setup portal");
        g_portal_mode = true;

        // Start AP
        wifi_portal_start();

        // Show portal instructions on display
        _show_portal_screen();

        log_i("Portal ready — loop() will service HTTP clients");
        return; // skip normal task spawn
    }

    // ── Normal boot ───────────────────────────────────────────

    // Build UI
    g_ui.init();

    // Wire print control buttons (touch → MQTT command) for each printer
    for (int i = 0; i < MAX_PRINTERS; i++) {
        int pi = i;
        g_ui.statusScreen(i).onPause ([pi]() { g_bambu[pi].pause();  });
        g_ui.statusScreen(i).onResume([pi]() { g_bambu[pi].resume(); });
        g_ui.statusScreen(i).onStop  ([pi]() { g_bambu[pi].stop();   });
    }

    // Wire calibrate button from WiFi config screen
    g_ui.onCalibrateWiFi([]() { g_run_calibration = true; });

    // Wire firmware upgrade button from WiFi config screen
    g_ui.onUpgradeWiFi([]() {
        if (ota_is_busy()) {
            LV_LOCK();
            g_ui.configWifiScreen().setStatusText(
                "OTA already in progress", lv_color_hex(0xE74C3C));
            LV_UNLOCK();
            return;
        }
        if (xTaskCreatePinnedToCore(ota_task, "ota_update", 32768,
                                    &g_ui.configWifiScreen(), 1, nullptr, 1)
            != pdPASS) {
            LV_LOCK();
            g_ui.configWifiScreen().setStatusText(
                "Failed to start OTA", lv_color_hex(0xE74C3C));
            LV_UNLOCK();
        }
    });

    // Wire config "save & connect" button from printer config screen
    g_ui.onSaveConnectPrinter([]() {
        loadCredentials();
        reconnectAll();
        g_ui.showOverview();
        g_ui.setNumPrinters(g_printer_count);
    });

    // Spawn network task on Core 1
    g_thumb_queue = xQueueCreate(8, sizeof(ThumbRequest));

    xTaskCreatePinnedToCore(networkTask, "network", 12288, nullptr, 2, nullptr, 1);

    // Spawn thumbnail task on Core 1
    xTaskCreatePinnedToCore(thumbTask,   "thumb",   20480, nullptr, 1, &g_thumb_task_handle, 1);

    log_i("Setup complete");
}

void loop() {
    // ── Portal mode: just drive LVGL + HTTP server ────────────
    if (g_portal_mode) {
        LV_LOCK();
        lv_timer_handler();
        LV_UNLOCK();
        wifi_portal_loop();
        delay(5);
        return;
    }

    // Calibration requested from config screen — run outside LVGL lock
    if (g_run_calibration) {
        g_run_calibration = false;
        // Hold the mutex so Core 1 doesn't touch LVGL during calibration
        LV_LOCK();
        touch_cal_run(lcd);   // draws directly on LCD, blocks until done
        // Restore display for LVGL (clear any LGFX-drawn pixels)
        lcd.fillScreen(TFT_BLACK);
        LV_UNLOCK();
        // Force a full LVGL redraw
        LV_LOCK();
        lv_obj_invalidate(lv_scr_act());
        LV_UNLOCK();
        return;
    }

    // Core 0: drive LVGL
    LV_LOCK();

    // Push decoded thumbnail if ready
    bool thumb_ready = false;
    int  thumb_pi    = -1;
    xSemaphoreTake(g_thumb_mutex, portMAX_DELAY);
    if (g_thumb_ready) {
        thumb_ready   = true;
        thumb_pi      = g_thumb_ready_pi;
        g_thumb_ready = false;
        g_thumb_ready_pi = -1;
    }
    xSemaphoreGive(g_thumb_mutex);

    if (thumb_ready && thumb_pi >= 0) {
        log_i("Pushing thumb to screens printer=%d", thumb_pi);
        g_ui.statusScreen(thumb_pi).setThumbnail(&g_thumb_dsc);
        g_ui.overviewScreen().setThumbnail(thumb_pi, &g_thumb_dsc);
    }

    lv_timer_handler();
    LV_UNLOCK();

    wifi_portal_loop();  // STA-mode config server (no-op if not started)
    delay(5);
}

// ─────────────────────────────────────────────────────────────
// OTA display overlay — shows progress on-screen when firmware
// update is triggered from the web UI (/update endpoint)
// ─────────────────────────────────────────────────────────────
static lv_obj_t *_ota_overlay     = nullptr;
static lv_obj_t *_ota_bar         = nullptr;
static lv_obj_t *_ota_msg         = nullptr;

void ota_display_begin() {
    LV_LOCK();
    if (!_ota_overlay) {
        _ota_overlay = lv_obj_create(lv_scr_act());
        lv_obj_set_size(_ota_overlay, LCD_WIDTH, LCD_HEIGHT);
        lv_obj_set_style_bg_color(_ota_overlay, lv_color_hex(0x1A1A2E), 0);
        lv_obj_set_style_border_width(_ota_overlay, 0, 0);
        lv_obj_clear_flag(_ota_overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_foreground(_ota_overlay);

        lv_obj_t *title = lv_label_create(_ota_overlay);
        lv_label_set_text(title, "Firmware Update");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0x1DB954), 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -80);

        _ota_bar = lv_bar_create(_ota_overlay);
        lv_obj_set_size(_ota_bar, 400, 24);
        lv_obj_align(_ota_bar, LV_ALIGN_CENTER, 0, -20);
        lv_bar_set_range(_ota_bar, 0, 100);
        lv_bar_set_value(_ota_bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(_ota_bar, lv_color_hex(0x0F3460), 0);
        lv_obj_set_style_border_color(_ota_bar, lv_color_hex(0x2D3561), 0);
        lv_obj_set_style_border_width(_ota_bar, 1, 0);
        lv_obj_set_style_radius(_ota_bar, 10, 0);
        lv_obj_set_style_pad_all(_ota_bar, 0, 0);

        _ota_msg = lv_label_create(_ota_overlay);
        lv_label_set_text(_ota_msg, "Receiving firmware…");
        lv_obj_set_style_text_font(_ota_msg, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(_ota_msg, lv_color_hex(0x90CAF9), 0);
        lv_obj_align(_ota_msg, LV_ALIGN_CENTER, 0, 24);
    } else {
        lv_obj_clear_flag(_ota_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(_ota_bar, 0, LV_ANIM_OFF);
        lv_label_set_text(_ota_msg, "Receiving firmware…");
    }
    LV_UNLOCK();
}

void ota_display_progress(int percent, const char *msg) {
    LV_LOCK();
    if (_ota_bar)  lv_bar_set_value(_ota_bar, percent, LV_ANIM_ON);
    if (_ota_msg)  lv_label_set_text(_ota_msg, msg);
    LV_UNLOCK();
}

void ota_display_end() {
    LV_LOCK();
    if (_ota_overlay) {
        lv_obj_add_flag(_ota_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    LV_UNLOCK();
}
