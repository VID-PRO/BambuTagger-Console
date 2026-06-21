#pragma once
/**
 * ui_manager.h  —  Top-level UI with left sidebar + screen switching
 *
 * Screen layout (800 × 480):
 * ┌───────┬────────────────────────────────────┐
 * │  80px │          720px                      │
 * │       │                                     │
 * │  Icon │    Active screen content            │
 * │  Menu │    (Overview, Printer, or Config)   │
 * │       │                                     │
 * └───────┴────────────────────────────────────┘
 *
 * Screens:
 *   OVERVIEW   — grid of printer cards (1, 2, or 2×2)
 *   PRINTER_n  — full-screen detail for one printer
 *   CONFIG_*   — WiFi / printer configuration
 */
#include <lvgl.h>
#include "../bambu/bambu_client.h"
#include "../config.h"
#include "screen_overview.h"
#include "screen_status.h"
#include "screen_config_wifi.h"
#include "screen_config_printer.h"
#include <functional>

enum class ActiveScreen {
    OVERVIEW,
    PRINTER_0, PRINTER_1, PRINTER_2, PRINTER_3,
    CONFIG_WIFI, CONFIG_PRINTER
};

class UIManager {
public:
    void init();

    /** Switch the main content area to screen s. */
    void showScreen(ActiveScreen s);

    /** Show overview grid (recalculates layout from num printers). */
    void showOverview();

    /** Show full-screen detail for printer idx (0..MAX_PRINTERS-1). */
    void showPrinter(int idx);

    /** Set the number of configured printers (1..4). */
    void setNumPrinters(int n);

    /** Route status update to the correct printer's overview card + detail screen. */
    void updateStatus(int idx, const PrinterStatus &s);

    /** Push a decoded thumbnail to a specific printer's detail screen. */
    void setThumbnail(int idx, const lv_img_dsc_t *dsc);

    /** WiFi indicator: green=connected, orange=connecting, red=down. */
    void setConnecting(bool connecting);

    /** MQTT/printer indicator: green=at least one connected, red=all down. */
    void setMqttConnected(bool connected);

    /** Wire up callbacks. */
    void onCalibrateWiFi(std::function<void()> cb) {
        _screen_config_wifi.onCalibrate(cb);
    }
    void onUpgradeWiFi(std::function<void()> cb) {
        _screen_config_wifi.onUpgrade(cb);
    }
    void onSaveConnectPrinter(std::function<void()> cb) {
        _screen_config_printer.onSaveConnect(cb);
    }

    ScreenStatus &statusScreen(int idx) { return _screen_status[idx]; }
    ScreenConfigWiFi &configWifiScreen() { return _screen_config_wifi; }
    ScreenConfigPrinter &configPrinterScreen() { return _screen_config_printer; }
    ScreenOverview &overviewScreen() { return _screen_overview; }

private:
    static void _sidebar_btn_cb(lv_event_t *e);

    lv_obj_t    *_sidebar         = nullptr;
    lv_obj_t    *_btn_status      = nullptr;
    lv_obj_t    *_btn_wifi        = nullptr;
    lv_obj_t    *_btn_printer     = nullptr;
    lv_obj_t    *_lbl_conn_dot    = nullptr;
    lv_obj_t    *_lbl_mqtt_dot    = nullptr;
    ActiveScreen _active          = ActiveScreen::OVERVIEW;
    int          _active_printer  = 0;
    int          _num_printers    = 1;

    ScreenStatus     _screen_status[MAX_PRINTERS];
    ScreenOverview   _screen_overview;
    ScreenConfigWiFi _screen_config_wifi;
    ScreenConfigPrinter _screen_config_printer;
};
