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

// ── Global LCD instance (declared extern in display_driver.h) ─
LGFX lcd;

// Flag set by config screen to trigger calibration from loop()
static volatile bool g_run_calibration = false;

// True when running in AP/portal mode (no network task, no UI)
static bool g_portal_mode = false;

// ── Global objects ────────────────────────────────────────────
static UIManager      g_ui;
static BambuClient    g_bambu;
static FtpsClient      g_ftp;

// Thumbnail state (written by FTP task, read by LVGL task)
static SemaphoreHandle_t g_thumb_mutex = nullptr;
static uint8_t           *g_thumb_buf  = nullptr;   // raw PNG bytes in PSRAM
static size_t             g_thumb_sz   = 0;
static lv_img_dsc_t       g_thumb_dsc  = {};
static uint8_t           *g_thumb_rgb  = nullptr;   // decoded RGB565 in PSRAM
static bool               g_thumb_ready= false;     // new frame available

// Credentials (loaded from NVS)
static char g_wifi_ssid[64]  = DEFAULT_WIFI_SSID;
static char g_wifi_pass[64]  = DEFAULT_WIFI_PASSWORD;
static char g_bam_ip[24]     = DEFAULT_BAMBU_IP;
static char g_bam_serial[32] = DEFAULT_BAMBU_SERIAL;
static char g_bam_code[16]   = DEFAULT_BAMBU_CODE;

// ── LVGL mutex (guards lv_* calls from multiple cores) ────────
static SemaphoreHandle_t g_lv_mutex = nullptr;

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
        prefs.getString("bam_ip",     g_bam_ip,     sizeof(g_bam_ip));
        prefs.getString("bam_serial", g_bam_serial, sizeof(g_bam_serial));
        prefs.getString("bam_code",   g_bam_code,   sizeof(g_bam_code));
    }
    prefs.end();
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
    g_bambu.begin(g_bam_ip, g_bam_serial, g_bam_code);
    g_ftp.begin(g_bam_ip, g_bam_code);
}

// ─────────────────────────────────────────────────────────────
// Thumbnail fetch task (runs on Core 1, triggered by flag)
// ─────────────────────────────────────────────────────────────
static TaskHandle_t g_thumb_task_handle = nullptr;
static char         g_thumb_job[64]      = {};
static char         g_thumb_gcode[128]   = {};

static void thumbTask(void *param) {
    for (;;) {
        // Wait for a signal (notified when a new job starts)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        log_i("Fetching thumbnail for: %s", g_thumb_job);

        // Allocate JPEG buffer if needed
        if (!g_thumb_buf) {
            g_thumb_buf = (uint8_t *)ps_malloc(256 * 1024); // 256 KB
            if (!g_thumb_buf) { log_e("ps_malloc thumb_buf failed"); continue; }
        }

        size_t png_sz = g_ftp.downloadThumbnail(g_thumb_job, g_thumb_gcode, g_thumb_buf, 256*1024);
        if (png_sz == 0) {
            log_w("Thumbnail download failed, showing placeholder");
            LV_LOCK();
            g_ui.statusScreen().clearThumbnail();
            LV_UNLOCK();
            continue;
        }
        // Decode PNG to RGB565 via LovyanGFX
        if (!g_thumb_rgb) {
            g_thumb_rgb = (uint8_t *)ps_malloc(THUMB_W * THUMB_H * 2);
            if (!g_thumb_rgb) { log_e("ps_malloc thumb_rgb failed"); continue; }
        }

        // Use LGFX sprite as intermediate decoder (PSRAM for pixel buffer)
        lgfx::LGFX_Sprite sprite(&lcd);
        sprite.setColorDepth(16);
        sprite.setPsram(true);   // allocate 240×240×2=115 KB from PSRAM, not DMA heap
        if (sprite.createSprite(THUMB_W, THUMB_H)) {
            // Read PNG dimensions from IHDR (bytes 16-23, big-endian)
            float scale = 1.0f;
            if (png_sz >= 24) {
                uint32_t png_w = ((uint32_t)g_thumb_buf[16]<<24)|((uint32_t)g_thumb_buf[17]<<16)
                                |((uint32_t)g_thumb_buf[18]<<8)|g_thumb_buf[19];
                uint32_t png_h = ((uint32_t)g_thumb_buf[20]<<24)|((uint32_t)g_thumb_buf[21]<<16)
                                |((uint32_t)g_thumb_buf[22]<<8)|g_thumb_buf[23];
                if (png_w > 0 && png_h > 0) {
                    float sx = (float)THUMB_W / png_w;
                    float sy = (float)THUMB_H / png_h;
                    scale = (sx < sy) ? sx : sy;   // fit, keep aspect ratio
                }
                log_i("PNG size %ux%u → scale %.3f → sprite %dx%d", png_w, png_h, scale, THUMB_W, THUMB_H);
            }
            sprite.drawPng(g_thumb_buf, png_sz, 0, 0, THUMB_W, THUMB_H,
                           0, 0, scale, scale, lgfx::datum_t::middle_center);
            // Read back RGB565
            sprite.readRect(0, 0, THUMB_W, THUMB_H, (lgfx::rgb565_t *)g_thumb_rgb);
            sprite.deleteSprite();

            // Build LVGL image descriptor
            g_thumb_dsc.header.always_zero = 0;
            g_thumb_dsc.header.w           = THUMB_W;
            g_thumb_dsc.header.h           = THUMB_H;
            g_thumb_dsc.header.cf          = LV_IMG_CF_TRUE_COLOR;
            g_thumb_dsc.data_size          = THUMB_W * THUMB_H * 2;
            g_thumb_dsc.data               = g_thumb_rgb;

            xSemaphoreTake(g_thumb_mutex, portMAX_DELAY);
            g_thumb_ready = true;
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

    // Configure Bambu client
    g_bambu.onStatus([](const PrinterStatus &s) {
        // Update UI (LVGL must be locked)
        LV_LOCK();
        g_ui.updateStatus(s);
        LV_UNLOCK();

        // New job → trigger thumbnail fetch
        if (s.fresh_thumb && strlen(s.job_name) > 0) {
            strncpy(g_thumb_job,   s.job_name,   sizeof(g_thumb_job)   - 1);
            strncpy(g_thumb_gcode, s.gcode_file, sizeof(g_thumb_gcode) - 1);
            // Const cast is safe here; notify does not modify status
            ((PrinterStatus &)s).fresh_thumb = false;
            if (g_thumb_task_handle) {
                xTaskNotifyGive(g_thumb_task_handle);
            }
        }
    });

    g_bambu.begin(g_bam_ip, g_bam_serial, g_bam_code);
    g_ftp.begin(g_bam_ip, g_bam_code);

    // Register "save & connect" callback from config screen
    // (already wired in setup via g_ui.onConnect)

    LV_LOCK();
    // Pre-fill config screens with current credentials
    g_ui.configWifiScreen().loadValues(g_wifi_ssid, g_wifi_pass);
    g_ui.configPrinterScreen().loadValues(g_bam_ip, g_bam_serial, g_bam_code);
    LV_UNLOCK();

    // Track last-known indicator state to avoid spamming LVGL
    bool last_wifi_ok = false;
    bool last_mqtt_ok = false;

    for (;;) {
        // Keep WiFi alive
        if (WiFi.status() != WL_CONNECTED) {
            connectWifi();  // sets icon orange→green internally
        }

        g_bambu.loop();

        // Only touch LVGL when the indicator state actually changes —
        // prevents constant lock contention that causes screen flicker.
        bool wifi_ok = (WiFi.status() == WL_CONNECTED);
        bool mqtt_ok = g_bambu.isConnected();
        if (wifi_ok != last_wifi_ok || mqtt_ok != last_mqtt_ok) {
            LV_LOCK();
            g_ui.setConnecting(!wifi_ok);
            g_ui.setMqttConnected(mqtt_ok);
            LV_UNLOCK();
            last_wifi_ok = wifi_ok;
            last_mqtt_ok = mqtt_ok;
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

    // Wire print control buttons (touch → MQTT command)
    g_ui.statusScreen().onPause ([]() { g_bambu.pause();  });
    g_ui.statusScreen().onResume([]() { g_bambu.resume(); });
    g_ui.statusScreen().onStop  ([]() { g_bambu.stop();   });

    // Wire calibrate button from WiFi config screen
    g_ui.onCalibrateWiFi([]() { g_run_calibration = true; });

    // Wire config "save & connect" button from printer config screen
    g_ui.onSaveConnectPrinter([]() {
        // Read new values from NVS (already written by screen_config_printer)
        Preferences prefs;
        prefs.begin("bambu_mon", true);
        prefs.getString("wifi_ssid",  g_wifi_ssid,  sizeof(g_wifi_ssid));
        prefs.getString("wifi_pass",  g_wifi_pass,  sizeof(g_wifi_pass));
        prefs.getString("bam_ip",     g_bam_ip,     sizeof(g_bam_ip));
        prefs.getString("bam_serial", g_bam_serial, sizeof(g_bam_serial));
        prefs.getString("bam_code",   g_bam_code,   sizeof(g_bam_code));
        prefs.end();
        // Reconnect with new settings
        reconnectAll();
        g_ui.showScreen(ActiveScreen::STATUS);
    });

    // Spawn network task on Core 1
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
    xSemaphoreTake(g_thumb_mutex, portMAX_DELAY);
    if (g_thumb_ready) {
        thumb_ready   = true;
        g_thumb_ready = false;
    }
    xSemaphoreGive(g_thumb_mutex);

    if (thumb_ready) {
        g_ui.setThumbnail(&g_thumb_dsc);
    }

    lv_timer_handler();
    LV_UNLOCK();

    wifi_portal_loop();  // STA-mode config server (no-op if not started)
    delay(5);
}
