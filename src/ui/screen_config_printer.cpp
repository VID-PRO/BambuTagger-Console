#include "screen_config_printer.h"
#include "config.h"
#include <Preferences.h>

#define COL_BG   lv_color_hex(0x1A1A2E)
#define COL_CARD lv_color_hex(0x16213E)
#define COL_TEXT lv_color_hex(0xEAEAEA)
#define COL_ACC  lv_color_hex(0x1DB954)

// ─────────────────────────────────────────────────────────────
void ScreenConfigPrinter::create(lv_obj_t *parent) {
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

    // ── Section: Printer ─────────────────────────────────────
    lv_obj_t *lbl_printer = lv_label_create(scroll);
    lv_label_set_text(lbl_printer, LV_SYMBOL_SETTINGS "  Printer Configuration");
    lv_obj_set_style_text_font(lbl_printer, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_printer, COL_ACC, 0);

    _ta_ip     = _makeField(scroll, nullptr, "IP Address (e.g. 192.168.1.100)", 0);
    _ta_serial = _makeField(scroll, _ta_ip,   "Serial Number (e.g. 01S00C...)",   0);
    _ta_code   = _makeField(scroll, _ta_serial, "Access Code (8 digits)",         0, true);

    // ── Status label ─────────────────────────────────────────
    _lbl_status = lv_label_create(scroll);
    lv_obj_set_style_text_font(_lbl_status, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_lbl_status, lv_color_hex(0x6C757D), 0);
    lv_label_set_text(_lbl_status, "");

    // ── Keyboard (hidden by default) ─────────────────────────
    _kb = lv_keyboard_create(_root);
    lv_obj_set_size(_kb, LCD_WIDTH - SIDEBAR_W, 200);
    lv_obj_align(_kb, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_flag(_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_kb, _kb_event_cb, LV_EVENT_ALL, this);

    // ── Bottom button row ─────────────────────────────────────
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
lv_obj_t *ScreenConfigPrinter::_makeField(lv_obj_t *parent, lv_obj_t *,
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
void ScreenConfigPrinter::loadValues(const char *ip, const char *serial,
                                     const char *code) {
    if (_ta_ip)     lv_textarea_set_text(_ta_ip,     ip);
    if (_ta_serial) lv_textarea_set_text(_ta_serial, serial);
    if (_ta_code)   lv_textarea_set_text(_ta_code,   code);
}

// ── Event: textarea focused → show keyboard ──────────────────
void ScreenConfigPrinter::_ta_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    auto *self = (ScreenConfigPrinter *)lv_event_get_user_data(e);

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
void ScreenConfigPrinter::_kb_event_cb(lv_event_t *e) {
    auto *self = (ScreenConfigPrinter *)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(self->_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

// ── Event: save button ────────────────────────────────────────
void ScreenConfigPrinter::_save_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;
    auto *self = (ScreenConfigPrinter *)lv_event_get_user_data(e);

    // Persist to NVS
    Preferences prefs;
    prefs.begin("bambu_mon", false);
    prefs.putString("bam_ip",     lv_textarea_get_text(self->_ta_ip));
    prefs.putString("bam_serial", lv_textarea_get_text(self->_ta_serial));
    prefs.putString("bam_code",   lv_textarea_get_text(self->_ta_code));
    prefs.end();

    lv_label_set_text(self->_lbl_status,
        LV_SYMBOL_OK "  Settings saved! Reconnecting…");
    lv_obj_set_style_text_color(self->_lbl_status,
        lv_color_hex(0x1DB954), 0);

    if (self->_save_cb) self->_save_cb();
}
