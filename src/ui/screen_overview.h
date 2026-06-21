#pragma once
/**
 * screen_overview.h  —  Printer grid overview
 *
 * Shows all configured printers in a grid layout:
 *   1 printer  → full-width card
 *   2 printers → side by side (2 × 1)
 *   3-4        → 2×2 grid
 *
 * Tap a card → show full-screen detail for that printer.
 */
#include <lvgl.h>
#include <functional>
#include "../bambu/bambu_client.h"
#include "../config.h"

class ScreenOverview {
public:
    void create(lv_obj_t *parent);

    /** Set number of printers and rebuild layout. */
    void setNumPrinters(int n);

    /** Update a specific card with live status. */
    void updateCard(int idx, const PrinterStatus &s);

    /** Push a decoded thumbnail to a card. */
    void setThumbnail(int idx, const lv_img_dsc_t *dsc);

    /** Register tap callback (passes printer index). */
    void onPrinterTap(std::function<void(int)> cb) { _tap_cb = cb; }

    lv_obj_t *root() const { return _root; }

private:
    struct Card {
        lv_obj_t *root         = nullptr;
        lv_obj_t *lbl_state    = nullptr;
        lv_obj_t *lbl_ip       = nullptr;
        lv_obj_t *lbl_job      = nullptr;
        lv_obj_t *img_thumb    = nullptr;
        lv_obj_t *lbl_nozzle   = nullptr;
        lv_obj_t *lbl_bed      = nullptr;
        lv_obj_t *lbl_chamber  = nullptr;
        lv_obj_t *lbl_remain   = nullptr;
        lv_obj_t *bar_prog     = nullptr;
        lv_obj_t *lbl_prog     = nullptr;
        uint8_t   *thumb_buf   = nullptr;   // scaled 100×129 RGB565
        lv_img_dsc_t thumb_dsc = {};        // descriptor pointing to thumb_buf
    } _cards[MAX_PRINTERS];

    lv_obj_t *_root       = nullptr;
    lv_obj_t *_lbl_empty  = nullptr;
    int       _num_printers = 0;
    std::function<void(int)> _tap_cb;

    void _buildLayout();
    void _card_event_cb(lv_event_t *e, int idx);
    static void _card_static_cb(lv_event_t *e);
};
