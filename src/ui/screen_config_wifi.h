#pragma once
/**
 * screen_config_wifi.h  —  WiFi Configuration Screen
 *
 * Lets the user set WiFi SSID and password via on-screen keyboard.
 * Values are persisted in NVS (Preferences).
 */
#include <lvgl.h>
#include <functional>

typedef std::function<void()> SaveCallback;

class ScreenConfigWiFi {
public:
    void create(lv_obj_t *parent);

    /** Called after the user taps "Save". */
    void onSave(SaveCallback cb) { _save_cb = cb; }

    /** Called after the user taps "Calibrate Touchscreen". */
    void onCalibrate(std::function<void()> cb) { _cal_cb = cb; }

    /** Called after the user taps "Firmware Upgrade". */
    void onUpgrade(std::function<void()> cb) { _upgrade_cb = cb; }

    // Populate fields from current NVS values
    void loadValues(const char *ssid, const char *pass);

    /** Update the status label text and colour (thread-safe with LV_LOCK). */
    void setStatusText(const char *text, lv_color_t color);

    /** Update dedicated progress overlay (outside scroll area, no layout flicker). */
    void setProgress(const char *text, lv_color_t color);

    lv_obj_t *root() const { return _root; }

private:
    static void _kb_event_cb(lv_event_t *e);
    static void _ta_event_cb(lv_event_t *e);
    static void _save_event_cb(lv_event_t *e);
    static void _cal_event_cb(lv_event_t *e);
    static void _upgrade_event_cb(lv_event_t *e);

    lv_obj_t *_makeField(lv_obj_t *parent, lv_obj_t *prev_ta,
                         const char *label_text, int y,
                         bool password = false);

    lv_obj_t        *_root        = nullptr;
    lv_obj_t        *_ta_ssid     = nullptr;
    lv_obj_t        *_ta_pass     = nullptr;
    lv_obj_t        *_kb          = nullptr;
    lv_obj_t        *_lbl_status  = nullptr;
    lv_obj_t        *_lbl_progress = nullptr;
    lv_obj_t        *_btn_upgrade = nullptr;
    SaveCallback     _save_cb;
    std::function<void()> _cal_cb;
    std::function<void()> _upgrade_cb;
};
