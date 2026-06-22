#include "ui_manager.h"
#include "config.h"
#include "logo_icon.h"
#include <string.h>
#include <esp_log.h>
#include <esp_heap_caps.h>

static const char *TAG = "ui_mgr";

// Convert v8 interleaved RGB565A8 ({R_low,R_high,A} per pixel) to v9 planar layout
static lv_img_dsc_t *_planar_rgb565a8(const lv_img_dsc_t *src) {
    uint32_t n = (uint32_t)src->header.w * src->header.h;
    uint8_t *planar = (uint8_t *)heap_caps_malloc(n * 3, MALLOC_CAP_SPIRAM);
    if (!planar) { ESP_LOGE(TAG, "OOM for planar conversion"); return NULL; }
    const uint8_t *v8 = src->data;
    for (uint32_t i = 0; i < n; i++) {
        planar[i * 2]         = v8[i * 3];     // R_low
        planar[i * 2 + 1]     = v8[i * 3 + 1]; // R_high
        planar[n * 2 + i]     = v8[i * 3 + 2]; // Alpha
    }
    lv_img_dsc_t *dsc = (lv_img_dsc_t *)heap_caps_malloc(sizeof(lv_img_dsc_t), MALLOC_CAP_SPIRAM);
    if (!dsc) { heap_caps_free(planar); return NULL; }
    memcpy(dsc, src, sizeof(lv_img_dsc_t));
    dsc->data = planar;
    return dsc;
}

#define COL_SIDEBAR     lv_color_hex(0x0F3460)
#define COL_BTN_ACTIVE  lv_color_hex(0x1DB954)
#define COL_BTN_IDLE    lv_color_hex(0x0F3460)
#define COL_ICON        lv_color_hex(0xEAEAEA)

// ─────────────────────────────────────────────────────────────
void UIManager::init() {
    lv_display_t *disp = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(
        disp,
        lv_color_hex(0x1DB954),
        lv_color_hex(0x3498DB),
        true,
        &lv_font_montserrat_18);
    lv_display_set_theme(disp, theme);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), 0);

    // ── Sidebar ───────────────────────────────────────────────
    _sidebar = lv_obj_create(scr);
    lv_obj_set_size(_sidebar, SIDEBAR_W, LCD_HEIGHT);
    lv_obj_align(_sidebar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_sidebar, COL_SIDEBAR, 0);
    lv_obj_set_style_bg_opa(_sidebar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_sidebar, 0, 0);
    lv_obj_set_style_pad_all(_sidebar, 0, 0);
    lv_obj_set_style_radius(_sidebar, 0, 0);
    lv_obj_clear_flag(_sidebar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(_sidebar, LV_OBJ_FLAG_CLICKABLE);

    // ── App logo / title strip ────────────────────────────────
    lv_obj_t *logo = lv_obj_create(_sidebar);
    lv_obj_set_size(logo, SIDEBAR_W, 60);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(logo, lv_color_hex(0x0A2647), 0);
    lv_obj_set_style_bg_opa(logo, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(logo, 0, 0);
    lv_obj_set_style_pad_all(logo, 0, 0);
    lv_obj_clear_flag(logo, LV_OBJ_FLAG_SCROLLABLE);

    static lv_img_dsc_t *logo_planar = NULL;
    if (!logo_planar) logo_planar = _planar_rgb565a8(&logo_icon);
    lv_obj_t *img_logo = lv_img_create(logo);
    lv_img_set_src(img_logo, logo_planar ? logo_planar : &logo_icon);
    lv_obj_center(img_logo);

    // ── Helper to build sidebar icon button ───────────────────
    auto makeSideBtn = [&](lv_obj_t *&btn_out, const char *icon, int y_off) {
        btn_out = lv_btn_create(_sidebar);
        lv_obj_set_size(btn_out, SIDEBAR_W - 8, 72);
        lv_obj_align(btn_out, LV_ALIGN_TOP_MID, 0, 68 + y_off);
        lv_obj_set_style_bg_color(btn_out, COL_BTN_IDLE, 0);
        lv_obj_set_style_bg_opa(btn_out, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn_out, 8, 0);
        lv_obj_set_style_border_width(btn_out, 0, 0);
        lv_obj_set_style_shadow_width(btn_out, 0, 0);
        lv_obj_clear_flag(btn_out, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(btn_out, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_add_event_cb(btn_out, _sidebar_btn_cb, LV_EVENT_CLICKED, this);

        lv_obj_t *ic = lv_label_create(btn_out);
        lv_label_set_text(ic, icon);
        lv_obj_set_style_text_font(ic, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(ic, COL_ICON, 0);
        lv_obj_align(ic, LV_ALIGN_CENTER, 0, -10);
    };

    makeSideBtn(_btn_status,  LV_SYMBOL_LIST,     0);
    makeSideBtn(_btn_printer, LV_SYMBOL_SETTINGS, 216);
    makeSideBtn(_btn_wifi,    LV_SYMBOL_WIFI,     280);

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

    // ── MQTT dot ──────────────────────────────────────────────
    _lbl_mqtt_dot = lv_label_create(_sidebar);
    lv_label_set_text(_lbl_mqtt_dot, LV_SYMBOL_EYE_CLOSE);
    lv_obj_set_style_text_font(_lbl_mqtt_dot, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lbl_mqtt_dot, lv_color_hex(0xE74C3C), 0);
    lv_obj_set_style_bg_opa(_lbl_mqtt_dot, LV_OPA_TRANSP, 0);
    lv_obj_align(_lbl_mqtt_dot, LV_ALIGN_BOTTOM_MID, 0, -40);

    // ── WiFi dot ──────────────────────────────────────────────
    _lbl_conn_dot = lv_label_create(_sidebar);
    lv_label_set_text(_lbl_conn_dot, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(_lbl_conn_dot, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lbl_conn_dot, lv_color_hex(0xE74C3C), 0);
    lv_obj_set_style_bg_opa(_lbl_conn_dot, LV_OPA_TRANSP, 0);
    lv_obj_align(_lbl_conn_dot, LV_ALIGN_BOTTOM_MID, 0, -10);

    // ── Create screens ────────────────────────────────────────
    _screen_overview.create(scr);
    for (int i = 0; i < MAX_PRINTERS; i++) {
        _screen_status[i].create(scr);
    }
    _screen_config_printer.create(scr);
    _screen_config_wifi.create(scr);

    // ── Wire overview card tap → show printer detail ─────────
    _screen_overview.onPrinterTap([this](int idx) {
        showPrinter(idx);
    });

    // ── Wire each status screen back button ──────────────────
    for (int i = 0; i < MAX_PRINTERS; i++) {
        _screen_status[i].onBack([this]() {
            showOverview();
        });
    }

    showOverview();
}

// ─────────────────────────────────────────────────────────────
void UIManager::showScreen(ActiveScreen s) {
    _active = s;

    // Hide all screens
    lv_obj_add_flag(_screen_overview.root(), LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < MAX_PRINTERS; i++) {
        lv_obj_add_flag(_screen_status[i].root(), LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(_screen_config_printer.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_screen_config_wifi.root(), LV_OBJ_FLAG_HIDDEN);

    // Reset button styles
    lv_obj_set_style_bg_color(_btn_status,  COL_BTN_IDLE, 0);
    lv_obj_set_style_bg_color(_btn_wifi,    COL_BTN_IDLE, 0);
    lv_obj_set_style_bg_color(_btn_printer, COL_BTN_IDLE, 0);

    switch (s) {
        case ActiveScreen::OVERVIEW:
            _screen_overview.setNumPrinters(_num_printers);
            lv_obj_clear_flag(_screen_overview.root(), LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(_btn_status, COL_BTN_ACTIVE, 0);
            break;

        case ActiveScreen::PRINTER_0:
        case ActiveScreen::PRINTER_1:
        case ActiveScreen::PRINTER_2:
        case ActiveScreen::PRINTER_3: {
            int pi = (int)s - (int)ActiveScreen::PRINTER_0;
            if (pi >= 0 && pi < MAX_PRINTERS) {
                _active_printer = pi;
                _screen_status[pi].setShowBack(_num_printers > 1);
                lv_obj_clear_flag(_screen_status[pi].root(), LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_set_style_bg_color(_btn_status, COL_BTN_ACTIVE, 0);
            break;
        }

        case ActiveScreen::CONFIG_WIFI:
            lv_obj_clear_flag(_screen_config_wifi.root(), LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(_btn_wifi, COL_BTN_ACTIVE, 0);
            break;

        case ActiveScreen::CONFIG_PRINTER:
            lv_obj_clear_flag(_screen_config_printer.root(), LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(_btn_printer, COL_BTN_ACTIVE, 0);
            break;
    }
}

// ─────────────────────────────────────────────────────────────
void UIManager::showOverview() {
    if (_num_printers == 1) {
        showPrinter(0);
        return;
    }
    showScreen(ActiveScreen::OVERVIEW);
}

void UIManager::showPrinter(int idx) {
    if (idx < 0 || idx >= MAX_PRINTERS) return;
    ActiveScreen s = (ActiveScreen)((int)ActiveScreen::PRINTER_0 + idx);
    showScreen(s);
}

// ─────────────────────────────────────────────────────────────
void UIManager::setNumPrinters(int n) {
    _num_printers = n;
    _screen_overview.setNumPrinters(n);
    if (n > 1) showOverview();
}

// ─────────────────────────────────────────────────────────────
void UIManager::updateStatus(int idx, const PrinterStatus &s) {
    if (idx < 0 || idx >= MAX_PRINTERS) return;
    _screen_overview.updateCard(idx, s);
    _screen_status[idx].update(s);
}

// ─────────────────────────────────────────────────────────────
void UIManager::setThumbnail(int idx, const lv_img_dsc_t *dsc) {
    if (idx >= 0 && idx < MAX_PRINTERS) {
        _screen_status[idx].setThumbnail(dsc);
    }
}

// ─────────────────────────────────────────────────────────────
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
        connected ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
}

// ── Sidebar button click ──────────────────────────────────────
void UIManager::_sidebar_btn_cb(lv_event_t *e) {
    auto *self = (UIManager *)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target_obj(e);

    if (btn == self->_btn_status) {
        self->showOverview();
    } else if (btn == self->_btn_wifi) {
        self->showScreen(ActiveScreen::CONFIG_WIFI);
    } else if (btn == self->_btn_printer) {
        self->showScreen(ActiveScreen::CONFIG_PRINTER);
    }
}
