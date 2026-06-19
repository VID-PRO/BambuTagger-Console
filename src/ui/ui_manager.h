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
#include "screen_config.h"
#include <functional>

enum class ActiveScreen { STATUS, CONFIG };

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

    /** Wire up the "save & connect" action from config screen. */
    void onConnect(ConnectCallback cb);

    ScreenStatus &statusScreen() { return _screen_status; }
    ScreenConfig &configScreen() { return _screen_config; }

private:
    static void _sidebar_btn_cb(lv_event_t *e);

    lv_obj_t    *_sidebar         = nullptr;
    lv_obj_t    *_btn_status      = nullptr;
    lv_obj_t    *_btn_config      = nullptr;
    lv_obj_t    *_lbl_conn_dot    = nullptr;
    lv_obj_t    *_lbl_mqtt_dot    = nullptr;
    ActiveScreen _active          = ActiveScreen::STATUS;

    ScreenStatus _screen_status;
    ScreenConfig _screen_config;
};
