#include "screen_config_wifi.h"
#include "config.h"
#include <Preferences.h>

#define COL_BG   lv_color_hex(0x1A1A2E)
#define COL_CARD lv_color_hex(0x16213E)
#define COL_TEXT lv_color_hex(0xEAEAEA)
#define COL_ACC  lv_color_hex(0x1DB954)

// ─────────────────────────────────────────────────────────────
void ScreenConfigWiFi::create(lv_obj_t *parent) {
    _root = lv_obj_create(parent);
    lv_obj_set_size(_root, LCD_WIDTH - SIDEBAR_W, LCD_HEIGHT);
    lv_obj_align(_root, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(_root, COL_BG, 0);
    lv_obj_set_style_border_width(_root, 0, 0);
    lv_obj_set_style_pad_all(_root, 0, 0);
    lv_obj_set_scrollbar_mode(_root, LV_SCROLLBAR_MODE_OFF);

    // ── Content scroller ─────────────────────────────────────
    lv_obj_t *scroll = lv_obj_create(_root);
    lv_obj_set_size(scroll, LCD_WIDTH - SIDEBAR_W, LCD_HEIGHT - 70);
    lv_obj_align(scroll, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(scroll, COL_BG, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 20, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(scroll, 6, 0);

    // ── Section: WiFi ─────────────────────────────────────────
    lv_obj_t *lbl_wifi = lv_label_create(scroll);
    lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI "  WiFi Configuration");
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_wifi, COL_ACC, 0);

    _ta_ssid = _makeField(scroll, nullptr, "SSID", 0);
    _ta_pass = _makeField(scroll, _ta_ssid, "Password", 0, true);

    // ── Status label ─────────────────────────────────────────
    _lbl_status = lv_label_create(scroll);
    lv_obj_set_style_text_font(_lbl_status, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_lbl_status, lv_color_hex(0x6C757D), 0);
    lv_label_set_text(_lbl_status, "");

    // ── Firmware Upgrade button ──────────────────────────────
    _btn_upgrade = lv_btn_create(scroll);
    lv_obj_set_width(_btn_upgrade, lv_pct(100));
    lv_obj_set_height(_btn_upgrade, 44);
    lv_obj_set_style_bg_color(_btn_upgrade, lv_color_hex(0x1DB954), 0);
    lv_obj_set_style_radius(_btn_upgrade, 8, 0);
    lv_obj_add_event_cb(_btn_upgrade, _upgrade_event_cb, LV_EVENT_CLICKED, this);

    lv_obj_t *up_lbl = lv_label_create(_btn_upgrade);
    lv_label_set_text(up_lbl, LV_SYMBOL_UPLOAD "  Firmware Upgrade");
    lv_obj_set_style_text_font(up_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(up_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(up_lbl);

    // ── Keyboard (hidden by default) ─────────────────────────
    _kb = lv_keyboard_create(_root);
    lv_obj_set_size(_kb, LCD_WIDTH - SIDEBAR_W, 200);
    lv_obj_align(_kb, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_flag(_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_kb, _kb_event_cb, LV_EVENT_ALL, this);

    // ── Progress overlay (outside scroll area → no layout flicker) ──
    _lbl_progress = lv_label_create(_root);
    lv_obj_set_width(_lbl_progress, LCD_WIDTH - SIDEBAR_W - 40);
    lv_obj_set_height(_lbl_progress, 30);
    lv_obj_align(_lbl_progress, LV_ALIGN_BOTTOM_LEFT, 20, -68);
    lv_obj_set_style_bg_color(_lbl_progress, COL_BG, 0);
    lv_obj_set_style_bg_opa(_lbl_progress, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(_lbl_progress, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(_lbl_progress, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(_lbl_progress, "");
    lv_obj_add_flag(_lbl_progress, LV_OBJ_FLAG_HIDDEN);

    // ── Bottom button row ─────────────────────────────────────
    // "Calibrate Touchscreen" (left)
    lv_obj_t *btn_cal = lv_btn_create(_root);
    lv_obj_set_size(btn_cal, 240, 44);
    lv_obj_align(btn_cal, LV_ALIGN_BOTTOM_LEFT, 20, -14);
    lv_obj_set_style_bg_color(btn_cal, lv_color_hex(0x334155), 0);  // slate
    lv_obj_add_event_cb(btn_cal, _cal_event_cb, LV_EVENT_CLICKED, this);

    lv_obj_t *cal_lbl = lv_label_create(btn_cal);
    lv_label_set_text(cal_lbl, LV_SYMBOL_EDIT "  Calibrate");
    lv_obj_set_style_text_font(cal_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(cal_lbl);

    // "Save" (right)
    lv_obj_t *btn = lv_btn_create(_root);
    lv_obj_set_size(btn, 200, 44);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -20, -14);
    lv_obj_set_style_bg_color(btn, COL_ACC, 0);
    lv_obj_add_event_cb(btn, _save_event_cb, LV_EVENT_CLICKED, this);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, LV_SYMBOL_SAVE "  Save");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(btn_lbl);
}

// ─────────────────────────────────────────────────────────────
lv_obj_t *ScreenConfigWiFi::_makeField(lv_obj_t *parent, lv_obj_t *,
                                        const char *label_text, int,
                                        bool password) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_width(cont, lv_pct(100));
    lv_obj_set_height(cont, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(cont, COL_CARD, 0);
    lv_obj_set_style_radius(cont, 6, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 8, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x8899AA), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *ta = lv_textarea_create(cont);
    lv_obj_set_width(ta, lv_pct(100));
    lv_obj_set_height(ta, 38);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, password);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x0F3460), 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ta, COL_TEXT, 0);
    lv_obj_set_style_radius(ta, 4, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0x2D3561), 0);
    lv_obj_align(ta, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(ta, _ta_event_cb, LV_EVENT_ALL, this);

    return ta;
}

// ─────────────────────────────────────────────────────────────
void ScreenConfigWiFi::loadValues(const char *ssid, const char *pass) {
    if (_ta_ssid)   lv_textarea_set_text(_ta_ssid,   ssid);
    if (_ta_pass)   lv_textarea_set_text(_ta_pass,   pass);
}

// ── Event: textarea focused → show keyboard ──────────────────
void ScreenConfigWiFi::_ta_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    auto *self = (ScreenConfigWiFi *)lv_event_get_user_data(e);

    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(self->_kb, ta);
        lv_obj_clear_flag(self->_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
    }
    if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(self->_kb, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(self->_kb, nullptr);
    }
}

// ── Event: keyboard ──────────────────────────────────────────
void ScreenConfigWiFi::_kb_event_cb(lv_event_t *e) {
    auto *self = (ScreenConfigWiFi *)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(self->_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

// ── Event: calibrate button ───────────────────────────────────
void ScreenConfigWiFi::_cal_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto *self = (ScreenConfigWiFi *)lv_event_get_user_data(e);
    if (self->_cal_cb) self->_cal_cb();
}

// ── Event: firmware upgrade button ────────────────────────────
void ScreenConfigWiFi::_upgrade_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto *self = (ScreenConfigWiFi *)lv_event_get_user_data(e);
    if (self->_upgrade_cb) self->_upgrade_cb();
}

// ── Update status label from any context (call with LV_LOCK) ──
void ScreenConfigWiFi::setStatusText(const char *text, lv_color_t color) {
    if (_lbl_status) {
        lv_label_set_text(_lbl_status, text);
        lv_obj_set_style_text_color(_lbl_status, color, 0);
    }
}

// ── Update progress overlay (outside scroll area) ──────────────
void ScreenConfigWiFi::setProgress(const char *text, lv_color_t color) {
    if (!_lbl_progress) return;
    if (text && text[0]) {
        lv_label_set_text(_lbl_progress, text);
        lv_obj_set_style_text_color(_lbl_progress, color, 0);
        lv_obj_clear_flag(_lbl_progress, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_lbl_progress, LV_OBJ_FLAG_HIDDEN);
    }
}

// ── Event: next button ────────────────────────────────────────
void ScreenConfigWiFi::_save_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;
    auto *self = (ScreenConfigWiFi *)lv_event_get_user_data(e);

    // Persist WiFi to NVS
    Preferences prefs;
    prefs.begin("bambu_mon", false);
    prefs.putString("wifi_ssid", lv_textarea_get_text(self->_ta_ssid));
    prefs.putString("wifi_pass", lv_textarea_get_text(self->_ta_pass));
    prefs.end();

    lv_label_set_text(self->_lbl_status, LV_SYMBOL_OK "  WiFi saved!");
    lv_obj_set_style_text_color(self->_lbl_status, lv_color_hex(0x1DB954), 0);

    if (self->_save_cb) self->_save_cb();
}
