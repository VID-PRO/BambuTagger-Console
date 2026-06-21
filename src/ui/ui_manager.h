#pragma once
/**
 * ui_manager.h  —  Top-level UI with left sidebar + screen switching
 *
 * Screen layout (800 × 480):
 * ┌───────┬────────────────────────────────────┐
 * │  80px │          720px                      │
 * │       │                                     │
 * │  Icon │    Active screen content            │
 * │  Menu │    (Status or Config)               │
 * │       │                                     │
 * └───────┴────────────────────────────────────┘
 */
#include <lvgl.h>
#include "../bambu/bambu_client.h"
#include "screen_status.h"
#include "screen_config_wifi.h"
#include "screen_config_printer.h"
#include <functional>

enum class ActiveScreen { STATUS, CONFIG_WIFI, CONFIG_PRINTER };

class UIManager {
public:
    void init();

    /** Switch the main content area to screen s. */
    void showScreen(ActiveScreen s);

    /** Push a fresh status snapshot to the status screen. */
    void updateStatus(const PrinterStatus &s);

    /** Update the thumbnail on the status screen. */
    void setThumbnail(const lv_img_dsc_t *dsc);

    /** WiFi indicator: green=connected, orange=connecting, red=down. */
    void setConnecting(bool connecting);

    /** MQTT/printer indicator: green=connected, red=disconnected. */
    void setMqttConnected(bool connected);

    /** Wire up the calibrate callback for WiFi config screen. */
    void onCalibrateWiFi(std::function<void()> cb) {
        _screen_config_wifi.onCalibrate(cb);
    }

    /** Wire up the firmware upgrade callback for WiFi config screen. */
    void onUpgradeWiFi(std::function<void()> cb) {
        _screen_config_wifi.onUpgrade(cb);
    }

    /** Wire up the save & connect callback for printer config screen. */
    void onSaveConnectPrinter(std::function<void()> cb) {
        _screen_config_printer.onSaveConnect(cb);
    }

    ScreenStatus &statusScreen() { return _screen_status; }
    ScreenConfigWiFi &configWifiScreen() { return _screen_config_wifi; }
    ScreenConfigPrinter &configPrinterScreen() { return _screen_config_printer; }

private:
    static void _sidebar_btn_cb(lv_event_t *e);

    lv_obj_t    *_sidebar         = nullptr;
    lv_obj_t    *_btn_status      = nullptr;
    lv_obj_t    *_btn_wifi        = nullptr;
    lv_obj_t    *_btn_printer     = nullptr;
    lv_obj_t    *_lbl_conn_dot    = nullptr;
    lv_obj_t    *_lbl_mqtt_dot    = nullptr;
    ActiveScreen _active          = ActiveScreen::STATUS;

    ScreenStatus _screen_status;
    ScreenConfigWiFi _screen_config_wifi;
    ScreenConfigPrinter _screen_config_printer;
};
