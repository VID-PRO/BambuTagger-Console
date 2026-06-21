#include "ui_manager.h"
#include "config.h"
#include "logo_icon.h"

#define COL_SIDEBAR     lv_color_hex(0x0F3460)
#define COL_BTN_ACTIVE  lv_color_hex(0x1DB954)
#define COL_BTN_IDLE    lv_color_hex(0x0F3460)
#define COL_ICON        lv_color_hex(0xEAEAEA)

// ─────────────────────────────────────────────────────────────
void UIManager::init() {
    // Apply default dark theme
    lv_theme_t *theme = lv_theme_default_init(
        lv_disp_get_default(),
        lv_color_hex(0x1DB954),   // primary (green)
        lv_color_hex(0x3498DB),   // secondary (blue)
        true,                     // dark mode
        &lv_font_montserrat_18);
    lv_disp_set_theme(lv_disp_get_default(), theme);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), 0);

    // ── Sidebar ───────────────────────────────────────────────
    _sidebar = lv_obj_create(scr);
    lv_obj_set_size(_sidebar, SIDEBAR_W, LCD_HEIGHT);
    lv_obj_align(_sidebar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_sidebar, COL_SIDEBAR, 0);
    lv_obj_set_style_border_width(_sidebar, 0, 0);
    lv_obj_set_style_pad_all(_sidebar, 0, 0);
    lv_obj_set_style_radius(_sidebar, 0, 0);
    lv_obj_clear_flag(_sidebar, LV_OBJ_FLAG_SCROLLABLE);

    // ── App logo / title strip ────────────────────────────────
    lv_obj_t *logo = lv_obj_create(_sidebar);
    lv_obj_set_size(logo, SIDEBAR_W, 60);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(logo, lv_color_hex(0x0A2647), 0);
    lv_obj_set_style_border_width(logo, 0, 0);
    lv_obj_set_style_pad_all(logo, 0, 0);
    lv_obj_clear_flag(logo, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *img_logo = lv_img_create(logo);
    lv_img_set_src(img_logo, &logo_icon);
    lv_obj_center(img_logo);

    // ── Helper to build sidebar icon button ───────────────────
    auto makeSideBtn = [&](lv_obj_t *&btn_out, const char *icon, int y_off) {
        btn_out = lv_btn_create(_sidebar);
        lv_obj_set_size(btn_out, SIDEBAR_W - 8, 72);
        lv_obj_align(btn_out, LV_ALIGN_TOP_MID, 0, 68 + y_off);
        lv_obj_set_style_bg_color(btn_out, COL_BTN_IDLE, 0);
        lv_obj_set_style_radius(btn_out, 8, 0);
        lv_obj_set_style_border_width(btn_out, 0, 0);
        lv_obj_set_style_shadow_width(btn_out, 0, 0);
        lv_obj_clear_flag(btn_out, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn_out, _sidebar_btn_cb, LV_EVENT_CLICKED, this);

        lv_obj_t *ic = lv_label_create(btn_out);
        lv_label_set_text(ic, icon);
        lv_obj_set_style_text_font(ic, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(ic, COL_ICON, 0);
        lv_obj_align(ic, LV_ALIGN_CENTER, 0, -10);  // shift up to make room for label
    };

    makeSideBtn(_btn_status,  LV_SYMBOL_LIST,     0);    // Printer / status
    makeSideBtn(_btn_printer, LV_SYMBOL_SETTINGS, 216);  // Printer config
    makeSideBtn(_btn_wifi,    LV_SYMBOL_WIFI,     280);  // WiFi config

    // ── Tooltip labels under icons ────────────────────────────
    auto makeTooltip = [&](lv_obj_t *btn, const char *txt) {
        lv_obj_t *t = lv_label_create(btn);
        lv_label_set_text(t, txt);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0x8899AA), 0);
        lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -4);
    };
    makeTooltip(_btn_status,  "Status");
    makeTooltip(_btn_printer, "Printer");
    makeTooltip(_btn_wifi,    "WiFi");

    // ── MQTT dot (above WiFi, bottom of sidebar) ─────────────
    _lbl_mqtt_dot = lv_label_create(_sidebar);
    lv_label_set_text(_lbl_mqtt_dot, LV_SYMBOL_EYE_CLOSE);
    lv_obj_set_style_text_font(_lbl_mqtt_dot, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lbl_mqtt_dot, lv_color_hex(0xE74C3C), 0);
    lv_obj_align(_lbl_mqtt_dot, LV_ALIGN_BOTTOM_MID, 0, -40);

    // ── WiFi dot (bottom of sidebar) ─────────────────────────
    _lbl_conn_dot = lv_label_create(_sidebar);
    lv_label_set_text(_lbl_conn_dot, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(_lbl_conn_dot, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lbl_conn_dot, lv_color_hex(0xE74C3C), 0);
    lv_obj_align(_lbl_conn_dot, LV_ALIGN_BOTTOM_MID, 0, -10);

    // ── Create screens (hidden by default) ───────────────────
    _screen_status.create(scr);
    _screen_config_printer.create(scr);
    _screen_config_wifi.create(scr);

    // ── Wire up config screen navigation ──────────────────────
    _screen_config_wifi.onCalibrate([this]() {
        // Calibration callback will be set in main.cpp
    });

    _screen_config_printer.onSaveConnect([this]() {
        // Connection logic will be set in main.cpp
    });

    showScreen(ActiveScreen::STATUS);
}

// ─────────────────────────────────────────────────────────────
void UIManager::showScreen(ActiveScreen s) {
    _active = s;

    // Hide all screens first
    lv_obj_add_flag(_screen_status.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_screen_config_printer.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_screen_config_wifi.root(), LV_OBJ_FLAG_HIDDEN);

    // Show/hide screens and update button states
    if (s == ActiveScreen::STATUS) {
        lv_obj_clear_flag(_screen_status.root(), LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(_btn_status,  COL_BTN_ACTIVE, 0);
        lv_obj_set_style_bg_color(_btn_wifi,    COL_BTN_IDLE,   0);
        lv_obj_set_style_bg_color(_btn_printer, COL_BTN_IDLE,   0);
    } else if (s == ActiveScreen::CONFIG_WIFI) {
        lv_obj_clear_flag(_screen_config_wifi.root(), LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(_btn_status,  COL_BTN_IDLE,   0);
        lv_obj_set_style_bg_color(_btn_wifi,    COL_BTN_ACTIVE, 0);
        lv_obj_set_style_bg_color(_btn_printer, COL_BTN_IDLE,   0);
    } else if (s == ActiveScreen::CONFIG_PRINTER) {
        lv_obj_clear_flag(_screen_config_printer.root(), LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(_btn_status,  COL_BTN_IDLE,   0);
        lv_obj_set_style_bg_color(_btn_wifi,    COL_BTN_IDLE,   0);
        lv_obj_set_style_bg_color(_btn_printer, COL_BTN_ACTIVE, 0);
    }
}

// ─────────────────────────────────────────────────────────────
void UIManager::updateStatus(const PrinterStatus &s) {
    _screen_status.update(s);
}

void UIManager::setThumbnail(const lv_img_dsc_t *dsc) {
    _screen_status.setThumbnail(dsc);
}

void UIManager::setConnecting(bool connecting) {
    lv_obj_set_style_text_color(
        _lbl_conn_dot,
        connecting ? lv_color_hex(0xF5A623) : lv_color_hex(0x1DB954),
        0);
}

void UIManager::setMqttConnected(bool connected) {
    lv_obj_set_style_text_color(
        _lbl_mqtt_dot,
        connected ? lv_color_hex(0x1DB954) : lv_color_hex(0xE74C3C),
        0);
    lv_label_set_text(
        _lbl_mqtt_dot, 
        connected ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE
    );
}

// ── Sidebar button click ──────────────────────────────────────
void UIManager::_sidebar_btn_cb(lv_event_t *e) {
    auto *self = (UIManager *)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target(e);

    if (btn == self->_btn_status) {
        self->showScreen(ActiveScreen::STATUS);
    } else if (btn == self->_btn_wifi) {
        self->showScreen(ActiveScreen::CONFIG_WIFI);
    } else if (btn == self->_btn_printer) {
        self->showScreen(ActiveScreen::CONFIG_PRINTER);
    }
}
