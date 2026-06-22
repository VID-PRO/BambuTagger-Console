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
    _scroll = lv_obj_create(_root);
    lv_obj_set_size(_scroll, LCD_WIDTH - SIDEBAR_W, LCD_HEIGHT - 70);
    lv_obj_align(_scroll, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_scroll, COL_BG, 0);
    lv_obj_set_style_border_width(_scroll, 0, 0);
    lv_obj_set_style_pad_all(_scroll, 20, 0);
    lv_obj_set_flex_flow(_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_scroll, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(_scroll, 8, 0);

    // ── Section: Printers ────────────────────────────────────
    lv_obj_t *lbl_header = lv_label_create(_scroll);
    lv_label_set_text(lbl_header, LV_SYMBOL_SETTINGS "  Printer Configuration");
    lv_obj_set_style_text_font(lbl_header, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_header, COL_ACC, 0);

    // ── Printer count row ────────────────────────────────────
    lv_obj_t *count_row = lv_obj_create(_scroll);
    lv_obj_set_width(count_row, lv_pct(100));
    lv_obj_set_height(count_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(count_row, COL_BG, 0);
    lv_obj_set_style_border_width(count_row, 0, 0);
    lv_obj_set_style_pad_all(count_row, 0, 0);
    lv_obj_clear_flag(count_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_count_label = lv_label_create(count_row);
    lv_label_set_text(lbl_count_label, "Number of printers:");
    lv_obj_set_style_text_font(lbl_count_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_count_label, COL_TEXT, 0);
    lv_obj_align(lbl_count_label, LV_ALIGN_LEFT_MID, 0, 0);

    _btn_minus = lv_btn_create(count_row);
    lv_obj_set_size(_btn_minus, 40, 40);
    lv_obj_set_style_bg_color(_btn_minus, lv_color_hex(0x334155), 0);
    lv_obj_set_style_radius(_btn_minus, 6, 0);
    lv_obj_set_style_border_width(_btn_minus, 0, 0);
    lv_obj_add_event_cb(_btn_minus, _count_btn_cb, LV_EVENT_CLICKED, this);
    lv_obj_align(_btn_minus, LV_ALIGN_RIGHT_MID, -140, 0);
    lv_obj_t *lbl_minus = lv_label_create(_btn_minus);
    lv_label_set_text(lbl_minus, "-");
    lv_obj_set_style_text_font(lbl_minus, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_minus, COL_TEXT, 0);
    lv_obj_center(lbl_minus);

    _lbl_count = lv_label_create(count_row);
    lv_label_set_text(_lbl_count, "1");
    lv_obj_set_style_text_font(_lbl_count, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(_lbl_count, COL_ACC, 0);
    lv_obj_align(_lbl_count, LV_ALIGN_RIGHT_MID, -100, 0);

    _btn_plus = lv_btn_create(count_row);
    lv_obj_set_size(_btn_plus, 40, 40);
    lv_obj_set_style_bg_color(_btn_plus, lv_color_hex(0x334155), 0);
    lv_obj_set_style_radius(_btn_plus, 6, 0);
    lv_obj_set_style_border_width(_btn_plus, 0, 0);
    lv_obj_add_event_cb(_btn_plus, _count_btn_cb, LV_EVENT_CLICKED, this);
    lv_obj_align(_btn_plus, LV_ALIGN_RIGHT_MID, -50, 0);
    lv_obj_t *lbl_plus = lv_label_create(_btn_plus);
    lv_label_set_text(lbl_plus, "+");
    lv_obj_set_style_text_font(lbl_plus, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_plus, COL_TEXT, 0);
    lv_obj_center(lbl_plus);

    // ── Status label ─────────────────────────────────────────
    _lbl_status = lv_label_create(_scroll);
    lv_obj_set_style_text_font(_lbl_status, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_lbl_status, lv_color_hex(0x6C757D), 0);
    lv_label_set_text(_lbl_status, "");

    // ── Keyboard (hidden by default) ─────────────────────────
    _kb = lv_keyboard_create(_root);
    lv_obj_set_size(_kb, LCD_WIDTH - SIDEBAR_W, 200);
    lv_obj_align(_kb, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_flag(_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_kb, _kb_event_cb, LV_EVENT_ALL, this);

    // ── Bottom: Save button ───────────────────────────────────
    lv_obj_t *btn = lv_btn_create(_root);
    lv_obj_set_size(btn, 200, 44);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -20, -14);
    lv_obj_set_style_bg_color(btn, COL_ACC, 0);
    lv_obj_add_event_cb(btn, _save_event_cb, LV_EVENT_CLICKED, this);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, LV_SYMBOL_SAVE "  Save");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(btn_lbl);

    // Initialise fields
    _rebuildFields();
}

// ─────────────────────────────────────────────────────────────
void ScreenConfigPrinter::_rebuildFields() {
    // Remove existing field containers
    for (int i = 0; i < MAX_PRINTERS; i++) {
        if (_fields[i].cont) {
            lv_obj_del(_fields[i].cont);
            _fields[i].cont = nullptr;
            _fields[i].ta_ip = nullptr;
            _fields[i].ta_serial = nullptr;
            _fields[i].ta_code = nullptr;
        }
    }

    // Create fields for each printer
    for (int i = 0; i < _num_printers; i++) {
        char title[32];
        snprintf(title, sizeof(title), LV_SYMBOL_SETTINGS "  Printer %d", i + 1);
        lv_obj_t *cont = lv_obj_create(_scroll);
        lv_obj_set_width(cont, lv_pct(100));
        lv_obj_set_height(cont, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(cont, COL_CARD, 0);
        lv_obj_set_style_radius(cont, 6, 0);
        lv_obj_set_style_border_width(cont, 0, 0);
        lv_obj_set_style_pad_all(cont, 8, 0);
        lv_obj_set_style_pad_left(cont, 12, 0);
        lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_foreground(cont);

        // Section title
        lv_obj_t *lbl_title = lv_label_create(cont);
        lv_label_set_text(lbl_title, title);
        lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(lbl_title, COL_ACC, 0);
        lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 0);

        // Place title, then fields below using manual positioning
        // (The container uses its own layout, so we just put fields at known y offsets)
        int fy = 36;
        _fields[i].ta_ip     = _makeField(cont, "IP Address (e.g. 192.168.1.100)");
        lv_obj_set_pos(_fields[i].ta_ip,     0, fy); fy += 56;
        _fields[i].ta_serial = _makeField(cont, "Serial Number (e.g. 01S00C...)");
        lv_obj_set_pos(_fields[i].ta_serial, 0, fy); fy += 56;
        _fields[i].ta_code   = _makeField(cont, "Access Code (8 digits)", true);
        lv_obj_set_pos(_fields[i].ta_code,   0, fy); fy += 56;

        lv_obj_set_height(cont, fy + 8);
        _fields[i].cont = cont;
    }

    // Ensure fields are inserted before status label (move status to end)
    lv_obj_move_foreground(_lbl_status);

    // Update +/- button states
    lv_obj_set_style_bg_color(_btn_minus,
        (_num_printers <= 1) ? lv_color_hex(0x1A1A2E) : lv_color_hex(0x334155), 0);
    lv_obj_set_style_bg_color(_btn_plus,
        (_num_printers >= MAX_PRINTERS) ? lv_color_hex(0x1A1A2E) : lv_color_hex(0x334155), 0);

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", _num_printers);
    lv_label_set_text(_lbl_count, buf);
}

// ─────────────────────────────────────────────────────────────
lv_obj_t *ScreenConfigPrinter::_makeField(lv_obj_t *parent,
                                           const char *label_text,
                                           bool password) {
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, lv_pct(100));
    lv_obj_set_height(ta, 38);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, password);
    lv_textarea_set_placeholder_text(ta, label_text);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x0F3460), 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ta, COL_TEXT, 0);
    lv_obj_set_style_radius(ta, 4, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0x2D3561), 0);
    lv_obj_set_style_pad_hor(ta, 8, 0);
    lv_obj_add_event_cb(ta, _ta_event_cb, LV_EVENT_ALL, this);

    return ta;
}

// ─────────────────────────────────────────────────────────────
void ScreenConfigPrinter::loadValues(const PrinterConfig configs[], int count) {
    _num_printers = count;
    _rebuildFields();
    for (int i = 0; i < count && i < MAX_PRINTERS; i++) {
        if (_fields[i].ta_ip)     lv_textarea_set_text(_fields[i].ta_ip,     configs[i].ip);
        if (_fields[i].ta_serial) lv_textarea_set_text(_fields[i].ta_serial, configs[i].serial);
        if (_fields[i].ta_code)   lv_textarea_set_text(_fields[i].ta_code,   configs[i].code);
    }
}

// ── Event: textarea focused → show keyboard ──────────────────
void ScreenConfigPrinter::_ta_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target_obj(e);
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

// ── Event: +/- count buttons ─────────────────────────────────
void ScreenConfigPrinter::_count_btn_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto *self = (ScreenConfigPrinter *)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target_obj(e);

    if (btn == self->_btn_plus && self->_num_printers < MAX_PRINTERS) {
        self->_num_printers++;
        self->_rebuildFields();
    } else if (btn == self->_btn_minus && self->_num_printers > 1) {
        self->_num_printers--;
        self->_rebuildFields();
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

    // Save count
    prefs.putInt("bam_count", self->_num_printers);

    // Save each printer's config
    for (int i = 0; i < self->_num_printers; i++) {
        char key[16];
        snprintf(key, sizeof(key), "bam_ip_%d", i);
        prefs.putString(key, self->_fields[i].ta_ip ?
            lv_textarea_get_text(self->_fields[i].ta_ip) : "");
        snprintf(key, sizeof(key), "bam_serial_%d", i);
        prefs.putString(key, self->_fields[i].ta_serial ?
            lv_textarea_get_text(self->_fields[i].ta_serial) : "");
        snprintf(key, sizeof(key), "bam_code_%d", i);
        prefs.putString(key, self->_fields[i].ta_code ?
            lv_textarea_get_text(self->_fields[i].ta_code) : "");
    }
    prefs.end();

    lv_label_set_text(self->_lbl_status,
        LV_SYMBOL_OK "  Settings saved! Reconnecting…");
    lv_obj_set_style_text_color(self->_lbl_status,
        lv_color_hex(0x1DB954), 0);

    if (self->_save_cb) self->_save_cb();
}
