#pragma once
/**
 * screen_config.h  —  Configuration screen
 *
 * Lets the user set WiFi credentials and printer details via
 * on-screen keyboard.  Values are persisted in NVS (Preferences).
 */
#include <lvgl.h>
#include <functional>

typedef std::function<void()> ConnectCallback;
typedef std::function<void()> CalibrateCallback;

class ScreenConfig {
public:
    void create(lv_obj_t *parent);

    /** Called after the user taps "Save & Connect". */
    void onConnect(ConnectCallback cb) { _connect_cb = cb; }

    /** Called after the user taps "Calibrate Touchscreen". */
    void onCalibrate(CalibrateCallback cb) { _cal_cb = cb; }

    // Populate fields from current NVS values
    void loadValues(const char *ssid, const char *pass,
                    const char *ip,   const char *serial,
                    const char *code);

    lv_obj_t *root() const { return _root; }

private:
    static void _kb_event_cb(lv_event_t *e);
    static void _ta_event_cb(lv_event_t *e);
    static void _save_event_cb(lv_event_t *e);
    static void _cal_event_cb(lv_event_t *e);

    lv_obj_t *_makeField(lv_obj_t *parent, lv_obj_t *prev_ta,
                         const char *label_text, int y,
                         bool password = false);

    lv_obj_t        *_root        = nullptr;
    lv_obj_t        *_ta_ssid     = nullptr;
    lv_obj_t        *_ta_pass     = nullptr;
    lv_obj_t        *_ta_ip       = nullptr;
    lv_obj_t        *_ta_serial   = nullptr;
    lv_obj_t        *_ta_code     = nullptr;
    lv_obj_t        *_kb          = nullptr;
    lv_obj_t        *_lbl_status  = nullptr;
    ConnectCallback  _connect_cb;
    CalibrateCallback _cal_cb;
};
