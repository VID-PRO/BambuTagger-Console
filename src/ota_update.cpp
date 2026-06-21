/**
 * ota_update.cpp — GitHub Release OTA update implementation
 */
#include "ota_update.h"
#include "config.h"
#include "ui/screen_config_wifi.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern SemaphoreHandle_t g_lv_mutex;

#define LV_LOCK()   xSemaphoreTake(g_lv_mutex, portMAX_DELAY)
#define LV_UNLOCK() xSemaphoreGive(g_lv_mutex)

// Clean exit from ota_task: release LVGL mutex if held, clear handle, delete self
#define OTA_EXIT() \
    do { \
        xSemaphoreGive(g_lv_mutex); /* best-effort release */ \
        g_ota_task_handle = nullptr; \
        vTaskDelay(pdMS_TO_TICKS(20)); \
        vTaskDelete(nullptr); \
    } while(0)

TaskHandle_t g_ota_task_handle = nullptr;

// ── Simple semver comparison (true if a > b) ──────────────────
static bool _ver_gt(const char *a, const char *b) {
    int ma, mb;
    while (*a && *b) {
        ma = mb = 0;
        while (*a >= '0' && *a <= '9') ma = ma * 10 + (*a++ - '0');
        while (*b >= '0' && *b <= '9') mb = mb * 10 + (*b++ - '0');
        if (ma != mb) return ma > mb;
        if (*a == '.' || *a == 'v') ++a;
        if (*b == '.' || *b == 'v') ++b;
    }
    return false;
}

static void _set_status(ScreenConfigWiFi *screen, const char *txt, uint32_t color) {
    LV_LOCK();
    screen->setStatusText(txt, lv_color_hex(color));
    LV_UNLOCK();
}

// ── OTA task ──────────────────────────────────────────────────
void ota_task(void *param) {
    ScreenConfigWiFi *screen = (ScreenConfigWiFi *)param;
    g_ota_task_handle = xTaskGetCurrentTaskHandle();

    _set_status(screen, "Checking for updates…", 0x90CAF9);

    // ── 1. Fetch latest release info ──────────────────────────
    {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.setTimeout(10000);
        http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

        if (!http.begin(client, "https://api.github.com/repos/VID-PRO/BambuTagger-Console/releases/latest")) {
            _set_status(screen, "HTTP begin failed", 0xE74C3C);
            OTA_EXIT();
            return;
        }
        http.addHeader("User-Agent", "BambuTagger-Console");
        http.addHeader("Accept", "application/vnd.github+json");

        int code = http.GET();
        if (code != 200) {
            char buf[64];
            snprintf(buf, sizeof(buf), "GitHub API error: %d", code);
            _set_status(screen, buf, 0xE74C3C);
            http.end();
            OTA_EXIT();
            return;
        }

        String payload = http.getString();
        http.end();

        // ── 2. Parse JSON ─────────────────────────────────────
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            _set_status(screen, "Failed to parse release info", 0xE74C3C);
            OTA_EXIT();
            return;
        }

        const char *tag = doc["tag_name"].as<const char *>();
        if (!tag || !*tag) {
            _set_status(screen, "No release tag found", 0xE74C3C);
            OTA_EXIT();
            return;
        }

        const char *ver = tag;
        while (*ver == 'v' || *ver == 'V') ++ver;

        if (!_ver_gt(ver, APP_VERSION)) {
            _set_status(screen, "Already up to date", 0x1DB954);
            vTaskDelay(pdMS_TO_TICKS(3000));
            _set_status(screen, "", 0x6C757D);
            OTA_EXIT();
            return;
        }

        // ── 3. Find BambuTagger-Console.ino.bin asset ─────────
        JsonArray assets = doc["assets"].as<JsonArray>();
        String downloadUrl;
        for (JsonObject asset : assets) {
            const char *name = asset["name"].as<const char *>();
            if (name && strcmp(name, "BambuTagger-Console.ino.bin") == 0) {
                downloadUrl = asset["browser_download_url"].as<String>();
                break;
            }
        }
        if (downloadUrl.length() == 0) {
            _set_status(screen, "Binary not found in release", 0xE74C3C);
            OTA_EXIT();
            return;
        }

        // ── 4. Download binary ────────────────────────────────
        _set_status(screen, "Downloading firmware…", 0x90CAF9);

        WiFiClientSecure client2;
        client2.setInsecure();
        HTTPClient http2;
        http2.setTimeout(30000);
        http2.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

        if (!http2.begin(client2, downloadUrl)) {
            _set_status(screen, "Download HTTP begin failed", 0xE74C3C);
            OTA_EXIT();
            return;
        }
        http2.addHeader("User-Agent", "BambuTagger-Console");

        code = http2.GET();
        if (code != 200) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Download error: %d", code);
            _set_status(screen, buf, 0xE74C3C);
            http2.end();
            OTA_EXIT();
            return;
        }

        int totalSize = http2.getSize();
        WiFiClient *stream = http2.getStreamPtr();

        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            _set_status(screen, "Update init failed", 0xE74C3C);
            http2.end();
            OTA_EXIT();
            return;
        }

        uint8_t buf[4096];
        int written  = 0;
        int last_pct = -1;

        while (http2.connected() && (totalSize <= 0 || written < totalSize)) {
            int avail = stream->available();
            if (avail > 0) {
                int toRead = (avail > (int)sizeof(buf)) ? (int)sizeof(buf) : avail;
                int read = stream->readBytes(buf, toRead);
                if (read <= 0) break;
                if (Update.write(buf, read) != read) {
                    _set_status(screen, "Flash write failed", 0xE74C3C);
                    Update.end(false);
                    http2.end();
                    OTA_EXIT();
                    return;
                }
                written += read;

                if (totalSize > 0) {
                    int pct = (written * 100) / totalSize;
                    if (pct != last_pct && (pct % 10 == 0 || pct == 100)) {
                        last_pct = pct;
                        char msg[48];
                        snprintf(msg, sizeof(msg), "Downloading… %d%%", pct);
                        _set_status(screen, msg, 0x90CAF9);
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        http2.end();

        // ── 5. Finish update ──────────────────────────────────
        if (!Update.end(true)) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Update failed: %s", Update.errorString());
            _set_status(screen, buf, 0xE74C3C);
            OTA_EXIT();
            return;
        }

        _set_status(screen, "Update complete! Rebooting…", 0x1DB954);
        vTaskDelay(pdMS_TO_TICKS(1500));
        ESP.restart();
    }

    OTA_EXIT();
}
