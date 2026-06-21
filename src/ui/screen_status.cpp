#include "screen_status.h"
#include "config.h"
#include "thumb_placeholder.h"
#include <stdio.h>

// ── Static style instances ────────────────────────────────────
lv_style_t ScreenStatus::_style_badge_run;
lv_style_t ScreenStatus::_style_badge_idle;
lv_style_t ScreenStatus::_style_badge_pause;
lv_style_t ScreenStatus::_style_badge_fail;
bool       ScreenStatus::_styles_init = false;

// ── Colour palette ────────────────────────────────────────────
#define COL_GREEN   lv_color_hex(0x1DB954)
#define COL_ORANGE  lv_color_hex(0xF5A623)
#define COL_RED     lv_color_hex(0xE74C3C)
#define COL_BLUE    lv_color_hex(0x3498DB)
#define COL_GREY    lv_color_hex(0x6C757D)
#define COL_BG      lv_color_hex(0x1A1A2E)
#define COL_CARD    lv_color_hex(0x16213E)
#define COL_TEXT    lv_color_hex(0xEAEAEA)
#define COL_SUBTEXT lv_color_hex(0x8899AA)

// ─────────────────────────────────────────────────────────────
void ScreenStatus::_initStyles() {
    if (_styles_init) return;
    _styles_init = true;

    auto initBadge = [](lv_style_t *st, lv_color_t bg) {
        lv_style_init(st);
        lv_style_set_bg_color(st, bg);
        lv_style_set_bg_opa(st, LV_OPA_COVER);
        lv_style_set_text_color(st, lv_color_white());
        lv_style_set_radius(st, 6);
        lv_style_set_pad_hor(st, 8);
        lv_style_set_pad_ver(st, 4);
        lv_style_set_border_width(st, 0);
    };
    initBadge(&_style_badge_run,   COL_GREEN);
    initBadge(&_style_badge_idle,  COL_GREY);
    initBadge(&_style_badge_pause, COL_ORANGE);
    initBadge(&_style_badge_fail,  COL_RED);
}

// ─────────────────────────────────────────────────────────────
void ScreenStatus::create(lv_obj_t *parent) {
    _initStyles();

    // Root container (fills right panel)
    _root = lv_obj_create(parent);
    lv_obj_set_size(_root, LCD_WIDTH - SIDEBAR_W, LCD_HEIGHT);
    lv_obj_align(_root, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(_root, COL_BG, 0);
    lv_obj_set_style_border_width(_root, 0, 0);
    lv_obj_set_style_pad_all(_root, 0, 0);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    // ── Header row ───────────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(_root);
    lv_obj_set_size(hdr, LCD_WIDTH - SIDEBAR_W, 60);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(hdr, COL_CARD, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_left(hdr, 16, 0);
    lv_obj_set_style_pad_right(hdr, 16, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    _lbl_job = lv_label_create(hdr);
    lv_obj_set_style_text_font(_lbl_job, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_lbl_job, COL_TEXT, 0);
    lv_label_set_text(_lbl_job, "No active print");
    lv_obj_align(_lbl_job, LV_ALIGN_LEFT_MID, 0, 0);
    lv_label_set_long_mode(_lbl_job, LV_LABEL_LONG_DOT);
    lv_obj_set_width(_lbl_job, 380);

    _lbl_state = lv_label_create(hdr);
    lv_obj_set_style_text_font(_lbl_state, &lv_font_montserrat_24, 0);
    lv_label_set_text(_lbl_state, "IDLE");
    lv_obj_align(_lbl_state, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_style(_lbl_state, &_style_badge_idle, 0);

    // ── Progress bar ─────────────────────────────────────────
    lv_obj_t *prog_row = lv_obj_create(_root);
    lv_obj_set_size(prog_row, LCD_WIDTH - SIDEBAR_W, 36);
    lv_obj_align(prog_row, LV_ALIGN_TOP_LEFT, 0, 60);
    lv_obj_set_style_bg_color(prog_row, COL_BG, 0);
    lv_obj_set_style_border_width(prog_row, 0, 0);
    lv_obj_set_style_pad_hor(prog_row, 16, 0);
    lv_obj_clear_flag(prog_row, LV_OBJ_FLAG_SCROLLABLE);

    _bar_prog = lv_bar_create(prog_row);
    lv_obj_set_size(_bar_prog, (LCD_WIDTH - SIDEBAR_W) - 100, 28);
    lv_obj_align(_bar_prog, LV_ALIGN_LEFT_MID, 0, 0);
    lv_bar_set_range(_bar_prog, 0, 100);
    lv_bar_set_value(_bar_prog, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(_bar_prog, lv_color_hex(0x2D3561), 0);
    lv_obj_set_style_bg_color(_bar_prog, COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(_bar_prog, 14, 0);
    lv_obj_set_style_radius(_bar_prog, 14, LV_PART_INDICATOR);

    _lbl_prog = lv_label_create(prog_row);
    lv_obj_set_style_text_font(_lbl_prog, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_lbl_prog, COL_GREEN, 0);
    lv_label_set_text(_lbl_prog, "0%");
    lv_obj_align(_lbl_prog, LV_ALIGN_RIGHT_MID, 0, 0);

    // ── Separator ────────────────────────────────────────────
    lv_obj_t *sep = lv_obj_create(_root);
    lv_obj_set_size(sep, LCD_WIDTH - SIDEBAR_W, 1);
    lv_obj_align(sep, LV_ALIGN_TOP_LEFT, 0, 97);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x2D3561), 0);
    lv_obj_set_style_border_width(sep, 0, 0);

    // ── Thumbnail ─────────────────────────────────────────────
    // Content area: y=98 to y=424 (326 px). Centre = 261.
    // thumb_cont = 248 px tall → top = 261 - 124 = 137
    lv_obj_t *thumb_cont = lv_obj_create(_root);
    lv_obj_set_size(thumb_cont, THUMB_W + 8, THUMB_H + 8);
    lv_obj_align(thumb_cont, LV_ALIGN_TOP_LEFT, 16, 137);
    lv_obj_set_style_bg_color(thumb_cont, COL_CARD, 0);
    lv_obj_set_style_radius(thumb_cont, 8, 0);
    lv_obj_set_style_border_width(thumb_cont, 0, 0);
    lv_obj_set_style_pad_all(thumb_cont, 4, 0);
    lv_obj_clear_flag(thumb_cont, LV_OBJ_FLAG_SCROLLABLE);

    _img_thumb = lv_img_create(thumb_cont);
    lv_obj_center(_img_thumb);
    lv_obj_set_size(_img_thumb, THUMB_W, THUMB_H);

    // ── Info panel (right of thumbnail) ──────────────────────
    int info_x = THUMB_W + 8 + 16 + 16;      // sidebar + left margin
    // info labels: 6 rows × 32 px + divider ≈ 208 px tall → top = 261 - 104 = 157
    int info_y = 157;
    int info_w = (LCD_WIDTH - SIDEBAR_W) - info_x - 16;

    auto makeInfoLabel = [&](lv_obj_t *&lbl, int y_off, const char *text) {
        lbl = lv_label_create(_root);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
        lv_label_set_text(lbl, text);
        lv_obj_set_pos(lbl, info_x, info_y + y_off);
        lv_obj_set_width(lbl, info_w);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    };

    makeInfoLabel(_lbl_nozzle,  0,   LV_SYMBOL_DOWNLOAD " Nozzle   --°C / --°C");
    makeInfoLabel(_lbl_bed,     32,  LV_SYMBOL_UPLOAD   " Bed      --°C / --°C");
    makeInfoLabel(_lbl_chamber, 64,  LV_SYMBOL_HOME     " Chamber  --°C");

    // Divider
    lv_obj_t *div2 = lv_obj_create(_root);
    lv_obj_set_size(div2, info_w, 1);
    lv_obj_set_pos(div2, info_x, info_y + 104);
    lv_obj_set_style_bg_color(div2, lv_color_hex(0x2D3561), 0);
    lv_obj_set_style_border_width(div2, 0, 0);

    makeInfoLabel(_lbl_layer, 116,  LV_SYMBOL_LIST     " Layers    -- / --");
    makeInfoLabel(_lbl_time,  148,  LV_SYMBOL_NEXT     " Remaining  --h --m");
    makeInfoLabel(_lbl_speed, 180,  LV_SYMBOL_PLAY     " Speed     100%");

    // ── Control bar (bottom of screen) ───────────────────────
    lv_obj_t *ctrl_bar = lv_obj_create(_root);
    lv_obj_set_size(ctrl_bar, LCD_WIDTH - SIDEBAR_W, 56);
    lv_obj_align(ctrl_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(ctrl_bar, COL_CARD, 0);
    lv_obj_set_style_border_width(ctrl_bar, 0, 0);
    lv_obj_set_style_pad_hor(ctrl_bar, 16, 0);
    lv_obj_clear_flag(ctrl_bar, LV_OBJ_FLAG_SCROLLABLE);

    // Pause / Resume button
    _btn_pause = lv_btn_create(ctrl_bar);
    lv_obj_set_size(_btn_pause, 168, 38);
    lv_obj_align(_btn_pause, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(_btn_pause, COL_ORANGE, 0);
    lv_obj_set_style_radius(_btn_pause, 8, 0);
    lv_obj_set_style_border_width(_btn_pause, 0, 0);
    lv_obj_set_style_shadow_width(_btn_pause, 0, 0);
    lv_obj_add_event_cb(_btn_pause, _pause_btn_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_flag(_btn_pause, LV_OBJ_FLAG_HIDDEN);

    _lbl_pause_btn = lv_label_create(_btn_pause);
    lv_label_set_text(_lbl_pause_btn, LV_SYMBOL_PAUSE "  Pause");
    lv_obj_set_style_text_font(_lbl_pause_btn, &lv_font_montserrat_24, 0);
    lv_obj_center(_lbl_pause_btn);

    // Stop button
    _btn_stop = lv_btn_create(ctrl_bar);
    lv_obj_set_size(_btn_stop, 148, 38);
    lv_obj_align(_btn_stop, LV_ALIGN_LEFT_MID, 184, 0);
    lv_obj_set_style_bg_color(_btn_stop, COL_RED, 0);
    lv_obj_set_style_radius(_btn_stop, 8, 0);
    lv_obj_set_style_border_width(_btn_stop, 0, 0);
    lv_obj_set_style_shadow_width(_btn_stop, 0, 0);
    lv_obj_add_event_cb(_btn_stop, _stop_btn_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_flag(_btn_stop, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *lbl_stop = lv_label_create(_btn_stop);
    lv_label_set_text(lbl_stop, LV_SYMBOL_STOP "  Stop Print");
    lv_obj_set_style_text_font(lbl_stop, &lv_font_montserrat_24, 0);
    lv_obj_center(lbl_stop);

    // ── Thumbnail placeholder ─────────────────────────────────
    lv_img_set_src(_img_thumb, &thumb_placeholder_dsc);

    // ── No-connection overlay ─────────────────────────────────
    _lbl_noconn = lv_label_create(_root);
    lv_obj_set_style_text_font(_lbl_noconn, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_lbl_noconn, COL_ORANGE, 0);
    lv_label_set_text(_lbl_noconn, LV_SYMBOL_WARNING "  Connecting to printer…");
    lv_obj_center(_lbl_noconn);
    lv_obj_add_flag(_lbl_noconn, LV_OBJ_FLAG_HIDDEN);
}

// ─────────────────────────────────────────────────────────────
void ScreenStatus::_applyBadgeStyle(PrintState state) {
    lv_obj_remove_style(_lbl_state, &_style_badge_run,   0);
    lv_obj_remove_style(_lbl_state, &_style_badge_idle,  0);
    lv_obj_remove_style(_lbl_state, &_style_badge_pause, 0);
    lv_obj_remove_style(_lbl_state, &_style_badge_fail,  0);

    switch (state) {
        case PrintState::RUNNING:
        case PrintState::PREPARE: lv_obj_add_style(_lbl_state, &_style_badge_run,   0); break;
        case PrintState::PAUSE:   lv_obj_add_style(_lbl_state, &_style_badge_pause, 0); break;
        case PrintState::FAILED:  lv_obj_add_style(_lbl_state, &_style_badge_fail,  0); break;
        default:                  lv_obj_add_style(_lbl_state, &_style_badge_idle,  0); break;
    }
}

// ─────────────────────────────────────────────────────────────
void ScreenStatus::update(const PrinterStatus &s) {
    // Hide "connecting" overlay
    lv_obj_add_flag(_lbl_noconn, LV_OBJ_FLAG_HIDDEN);

    // Job name
    if (strlen(s.job_name) > 0)
        lv_label_set_text(_lbl_job, s.job_name);
    else
        lv_label_set_text(_lbl_job, "No active print");

    // State badge
    lv_label_set_text(_lbl_state, s.state_str[0] ? s.state_str : "IDLE");
    _applyBadgeStyle(s.state);

    // Progress bar
    lv_bar_set_value(_bar_prog, s.progress, LV_ANIM_ON);
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", s.progress);
    lv_label_set_text(_lbl_prog, buf);

    // Temperatures
    char tmp[48];
    snprintf(tmp, sizeof(tmp), LV_SYMBOL_DOWNLOAD " Nozzle   %.0f°C / %.0f°C",
             s.temp_nozzle, s.temp_nozzle_t);
    lv_label_set_text(_lbl_nozzle, tmp);

    snprintf(tmp, sizeof(tmp), LV_SYMBOL_UPLOAD " Bed      %.0f°C / %.0f°C",
             s.temp_bed, s.temp_bed_t);
    lv_label_set_text(_lbl_bed, tmp);

    snprintf(tmp, sizeof(tmp), LV_SYMBOL_HOME " Chamber  %.0f°C", s.temp_chamber);
    lv_label_set_text(_lbl_chamber, tmp);

    // Layers
    snprintf(tmp, sizeof(tmp), LV_SYMBOL_LIST " Layers    %u / %u",
             s.layer_cur, s.layer_total);
    lv_label_set_text(_lbl_layer, tmp);

    // Remaining time
    uint16_t h = s.remaining_min / 60;
    uint16_t m = s.remaining_min % 60;
    if (h > 0)
        snprintf(tmp, sizeof(tmp), LV_SYMBOL_NEXT " Remaining  %uh %02um", h, m);
    else
        snprintf(tmp, sizeof(tmp), LV_SYMBOL_NEXT " Remaining  %um", m);
    lv_label_set_text(_lbl_time, tmp);

    // Speed
    snprintf(tmp, sizeof(tmp), LV_SYMBOL_PLAY " Speed     %u%%", s.speed_pct);
    lv_label_set_text(_lbl_speed, tmp);

    // Control buttons (Pause / Resume / Stop)
    _updateControls(s.state);
}

// ─────────────────────────────────────────────────────────────
void ScreenStatus::setThumbnail(const lv_img_dsc_t *img_dsc) {
    if (!img_dsc) return;
    lv_img_set_src(_img_thumb, img_dsc);
    lv_obj_invalidate(_img_thumb);
}

void ScreenStatus::clearThumbnail() {
    lv_img_set_src(_img_thumb, &thumb_placeholder_dsc);
    lv_obj_invalidate(_img_thumb);
}

// ─────────────────────────────────────────────────────────────
void ScreenStatus::_updateControls(PrintState state) {
    bool active = (state == PrintState::RUNNING ||
                   state == PrintState::PAUSE   ||
                   state == PrintState::PREPARE);

    if (active) {
        lv_obj_clear_flag(_btn_pause, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_btn_stop,  LV_OBJ_FLAG_HIDDEN);

        if (state == PrintState::PAUSE) {
            lv_label_set_text(_lbl_pause_btn, LV_SYMBOL_PLAY "  Resume");
            lv_obj_set_style_bg_color(_btn_pause, COL_GREEN, 0);
        } else {
            lv_label_set_text(_lbl_pause_btn, LV_SYMBOL_PAUSE "  Pause");
            lv_obj_set_style_bg_color(_btn_pause, COL_ORANGE, 0);
        }
    } else {
        lv_obj_add_flag(_btn_pause, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_btn_stop,  LV_OBJ_FLAG_HIDDEN);
    }
    _last_state = state;
}

// ── Pause / Resume button handler ────────────────────────────
void ScreenStatus::_pause_btn_cb(lv_event_t *e) {
    auto *self = (ScreenStatus *)lv_event_get_user_data(e);
    if (self->_last_state == PrintState::PAUSE) {
        if (self->_resume_cb) self->_resume_cb();
    } else {
        if (self->_pause_cb)  self->_pause_cb();
    }
}

// ── Stop button handler ───────────────────────────────────────
void ScreenStatus::_stop_btn_cb(lv_event_t *e) {
    auto *self = (ScreenStatus *)lv_event_get_user_data(e);
    if (self->_stop_cb) self->_stop_cb();
}
