#pragma once
/**
 * screen_status.h  —  Printer status screen
 *
 * Layout (720 × 480):
 * ┌──────────────────────────────────────────────┐
 * │  Job name                    [STATE BADGE]   │
 * │  ████████████████░░░░░░  85%                 │
 * ├──────────────────────────────────────────────┤
 * │  ┌──────────┐   🌡 Nozzle  220°C / 220°C     │
 * │  │          │   🛏 Bed      55°C /  55°C     │
 * │  │  THUMB   │   🏠 Chamber  30°C             │
 * │  │          │                                │
 * │  └──────────┘   Layers: 85 / 100             │
 * │                 Remaining: 1h 23m            │
 * │                 Speed: 100%                  │
 * └──────────────────────────────────────────────┘
 */
#include <lvgl.h>
#include <functional>
#include "../bambu/bambu_client.h"

typedef std::function<void()> PrintControlCallback;

class ScreenStatus {
public:
    /** Create all LVGL objects inside parent container. */
    void create(lv_obj_t *parent);

    /** Refresh all labels/bar from a PrinterStatus snapshot. */
    void update(const PrinterStatus &s);

    /** Update thumbnail from a decoded RGB565 buffer (THUMB_W × THUMB_H). */
    void setThumbnail(const lv_img_dsc_t *img_dsc);

    /** Show a "no thumbnail" placeholder. */
    void clearThumbnail();

    /** Register print control callbacks (called from touch events). */
    void onPause (PrintControlCallback cb) { _pause_cb  = cb; }
    void onResume(PrintControlCallback cb) { _resume_cb = cb; }
    void onStop  (PrintControlCallback cb) { _stop_cb   = cb; }

    /** Show/hide back button + set callback (calls showOverview in UIManager). */
    void setShowBack(bool show) { _show_back = show; }
    void onBack(std::function<void()> cb) { _back_cb = cb; }

    /** Set the printer IP shown in the header. */
    void setPrinterName(const char *name);

    lv_obj_t *root() const { return _root; }

private:
    static void _pause_btn_cb(lv_event_t *e);
    static void _stop_btn_cb (lv_event_t *e);
    static void _back_btn_cb (lv_event_t *e);

    void _updateControls(PrintState state);

    lv_obj_t *_root           = nullptr;
    lv_obj_t *_lbl_job        = nullptr;
    lv_obj_t *_lbl_state      = nullptr;
    lv_obj_t *_bar_prog       = nullptr;
    lv_obj_t *_lbl_prog       = nullptr;
    lv_obj_t *_img_thumb      = nullptr;
    lv_obj_t *_lbl_nozzle     = nullptr;
    lv_obj_t *_lbl_bed        = nullptr;
    lv_obj_t *_lbl_chamber    = nullptr;
    lv_obj_t *_lbl_layer      = nullptr;
    lv_obj_t *_lbl_time       = nullptr;
    lv_obj_t *_lbl_speed      = nullptr;
    lv_obj_t *_lbl_noconn     = nullptr;
    lv_obj_t *_lbl_printer    = nullptr;
    lv_obj_t *_btn_back       = nullptr;

    // Control buttons
    lv_obj_t *_btn_pause      = nullptr;  // doubles as Resume
    lv_obj_t *_btn_stop       = nullptr;
    lv_obj_t *_lbl_pause_btn  = nullptr;  // label inside _btn_pause
    bool      _show_back      = false;
    PrintState _last_state     = PrintState::UNKNOWN;

    PrintControlCallback _pause_cb;
    PrintControlCallback _resume_cb;
    PrintControlCallback _stop_cb;
    std::function<void()> _back_cb;

    static lv_style_t _style_badge_run;
    static lv_style_t _style_badge_idle;
    static lv_style_t _style_badge_pause;
    static lv_style_t _style_badge_fail;
    static bool       _styles_init;
    void _initStyles();
    void _applyBadgeStyle(PrintState state);
};
