#include "screen_overview.h"
#include "thumb_placeholder.h"
#include <esp_log.h>

#define COL_BG   lv_color_hex(0x1A1A2E)
#define COL_CARD lv_color_hex(0x16213E)
#define COL_TEXT lv_color_hex(0xEAEAEA)
#define COL_ACC  lv_color_hex(0x1DB954)
#define COL_RED  lv_color_hex(0xE74C3C)
#define COL_GREY lv_color_hex(0x6C757D)
#define COL_ORANGE lv_color_hex(0xF5A623)

#define CONTENT_W  (LCD_WIDTH - SIDEBAR_W)   // 720
#define CONTENT_H  LCD_HEIGHT                 // 480

// ── Pre-scaled placeholder (240×240 → 100×129, TRUE_COLOR_ALPHA) ──
static lv_img_dsc_t _scaled_place = {};
static bool _scaled_place_init = false;

static void _init_scaled_place() {
    if (_scaled_place_init) return;
    _scaled_place_init = true;
    int ow = 100, oh = 100, sw = 240, sh = 240;
    size_t sz = ow * oh * 3; // TRUE_COLOR_ALPHA: 3B/px
    uint8_t *buf = (uint8_t *)ps_malloc(sz);
    if (!buf) { log_e("ps_malloc scaled placeholder failed"); return; }
    const uint8_t *src = (const uint8_t *)thumb_placeholder_dsc.data;
    for (int oy = 0; oy < oh; oy++) {
        int sy = (oy * sh + oh / 2) / oh;
        for (int ox = 0; ox < ow; ox++) {
            int sx = (ox * sw + ow / 2) / ow;
            int si = (sy * sw + sx) * 3;
            int oi = (oy * ow + ox) * 3;
            buf[oi]     = src[si];
            buf[oi + 1] = src[si + 1];
            buf[oi + 2] = src[si + 2]; // preserve alpha
        }
    }
    _scaled_place.header.w   = ow;
    _scaled_place.header.h   = oh;
    _scaled_place.header.cf  = LV_IMG_CF_TRUE_COLOR_ALPHA;
    _scaled_place.data_size  = sz;
    _scaled_place.data       = buf;
}

// ─────────────────────────────────────────────────────────────
void ScreenOverview::create(lv_obj_t *parent) {
    _root = lv_obj_create(parent);
    lv_obj_set_size(_root, CONTENT_W, CONTENT_H);
    lv_obj_align(_root, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(_root, COL_BG, 0);
    lv_obj_set_style_border_width(_root, 0, 0);
    lv_obj_set_style_pad_all(_root, 0, 0);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    // Empty-state label
    _lbl_empty = lv_label_create(_root);
    lv_label_set_text(_lbl_empty, LV_SYMBOL_SETTINGS "  No printers configured");
    lv_obj_set_style_text_font(_lbl_empty, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_lbl_empty, COL_GREY, 0);
    lv_obj_center(_lbl_empty);
}

// ─────────────────────────────────────────────────────────────
void ScreenOverview::setNumPrinters(int n) {
    _num_printers = (n > MAX_PRINTERS) ? MAX_PRINTERS : (n < 0 ? 0 : n);

    if (_num_printers == 0) {
        lv_obj_clear_flag(_lbl_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(_lbl_empty, LV_OBJ_FLAG_HIDDEN);
    _buildLayout();
}

// ─────────────────────────────────────────────────────────────
void ScreenOverview::_buildLayout() {
    int cols = (_num_printers == 1) ? 1 : 2;
    int rows = (_num_printers <= 2) ? _num_printers : 2;

    int gap  = 10;
    int cw   = (CONTENT_W - gap * (cols + 1)) / cols;
    int ch   = (CONTENT_H - gap * (rows + 1)) / rows;

    // Thumbnail display size (square)
    int thumb_w = 100;
    int thumb_h = thumb_w;

    // Thumbnail y: centred vertically between job-name bottom (~56) and progress-bar top (ch-18)
    int thumb_y = 56 + ((ch - 18 - 56) - thumb_h) / 2;

    for (int i = 0; i < _num_printers; i++) {
        int col = i % cols;
        int row = i / cols;
        int x   = gap + col * (cw + gap);
        int y   = gap + row * (ch + gap);

        if (!_cards[i].root) {
            // ── Card container ────────────────────────────────
            _cards[i].root = lv_obj_create(_root);
            lv_obj_set_size(_cards[i].root, cw, ch);
            lv_obj_set_pos(_cards[i].root, x, y);
            lv_obj_set_style_bg_color(_cards[i].root, COL_CARD, 0);
            lv_obj_set_style_radius(_cards[i].root, 8, 0);
            lv_obj_set_style_border_width(_cards[i].root, 0, 0);
            lv_obj_set_style_pad_all(_cards[i].root, 8, 0);
            lv_obj_set_style_shadow_width(_cards[i].root, 4, 0);
            lv_obj_set_style_shadow_color(_cards[i].root, lv_color_hex(0x000000), 0);
            lv_obj_set_style_shadow_opa(_cards[i].root, LV_OPA_30, 0);
            lv_obj_clear_flag(_cards[i].root, LV_OBJ_FLAG_SCROLLABLE);

            // Card is clickable
            lv_obj_add_event_cb(_cards[i].root, _card_static_cb, LV_EVENT_CLICKED, this);

            // Content area (after 8px padding) = cw-16 × ch-16
            int ca_w = cw - 16;
            int ca_h = ch - 16;

            // ── State badge (top-right) ──────────────────────
            _cards[i].lbl_state = lv_label_create(_cards[i].root);
            lv_obj_set_style_text_font(_cards[i].lbl_state, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(_cards[i].lbl_state, COL_GREY, 0);
            lv_obj_align(_cards[i].lbl_state, LV_ALIGN_TOP_RIGHT, -4, 4);
            lv_label_set_text(_cards[i].lbl_state, "---");

            // ── Printer name (top-left) ──────────────────────
            _cards[i].lbl_ip = lv_label_create(_cards[i].root);
            lv_obj_set_style_text_font(_cards[i].lbl_ip, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(_cards[i].lbl_ip, COL_TEXT, 0);
            lv_obj_align(_cards[i].lbl_ip, LV_ALIGN_TOP_LEFT, 4, 4);
            lv_label_set_text(_cards[i].lbl_ip, "---");

            // ── Job name (full width above thumbnail) ────────
            int info_x = 4 + thumb_w + 8;
            int job_w  = ca_w - 8;
            _cards[i].lbl_job = lv_label_create(_cards[i].root);
            lv_obj_set_style_text_font(_cards[i].lbl_job, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(_cards[i].lbl_job, COL_ACC, 0);
            lv_obj_set_pos(_cards[i].lbl_job, 4, 32);
            lv_obj_set_width(_cards[i].lbl_job, job_w);
            lv_label_set_long_mode(_cards[i].lbl_job, LV_LABEL_LONG_DOT);
            lv_label_set_text(_cards[i].lbl_job, "No active print");

            // ── Thumbnail image (left side) ───────────────────
            _init_scaled_place();
            _cards[i].img_thumb = lv_img_create(_cards[i].root);
            lv_obj_set_pos(_cards[i].img_thumb, 4, thumb_y);
            lv_obj_set_size(_cards[i].img_thumb, thumb_w, thumb_h);
            lv_obj_set_style_radius(_cards[i].img_thumb, 4, 0);
            lv_obj_set_style_clip_corner(_cards[i].img_thumb, true, 0);
            lv_img_set_src(_cards[i].img_thumb, &_scaled_place);

            // ── Temperature labels beside thumbnail ───────────
            _cards[i].lbl_nozzle = lv_label_create(_cards[i].root);
            lv_obj_set_style_text_font(_cards[i].lbl_nozzle, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(_cards[i].lbl_nozzle, COL_TEXT, 0);
            lv_obj_set_pos(_cards[i].lbl_nozzle, info_x, thumb_y);
            lv_label_set_text(_cards[i].lbl_nozzle, LV_SYMBOL_DOWNLOAD " --°C");

            _cards[i].lbl_bed = lv_label_create(_cards[i].root);
            lv_obj_set_style_text_font(_cards[i].lbl_bed, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(_cards[i].lbl_bed, COL_TEXT, 0);
            lv_obj_set_pos(_cards[i].lbl_bed, info_x, thumb_y + 30);
            lv_label_set_text(_cards[i].lbl_bed, LV_SYMBOL_UPLOAD " --°C");

            _cards[i].lbl_chamber = lv_label_create(_cards[i].root);
            lv_obj_set_style_text_font(_cards[i].lbl_chamber, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(_cards[i].lbl_chamber, COL_TEXT, 0);
            lv_obj_set_pos(_cards[i].lbl_chamber, info_x, thumb_y + 60);
            lv_label_set_text(_cards[i].lbl_chamber, LV_SYMBOL_HOME " --°C");

            // ── Remaining time ─────────────────────────────────
            _cards[i].lbl_remain = lv_label_create(_cards[i].root);
            lv_obj_set_style_text_font(_cards[i].lbl_remain, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(_cards[i].lbl_remain, COL_GREY, 0);
            lv_obj_set_pos(_cards[i].lbl_remain, info_x, thumb_y + 90);
            lv_label_set_text(_cards[i].lbl_remain, "");

            // ── Progress bar (bottom) ─────────────────────────
            _cards[i].bar_prog = lv_bar_create(_cards[i].root);
            lv_obj_set_size(_cards[i].bar_prog, ca_w - 80, 14);
            lv_obj_set_style_bg_color(_cards[i].bar_prog, lv_color_hex(0x2D3561), 0);
            lv_obj_set_style_bg_color(_cards[i].bar_prog, COL_ACC, LV_PART_INDICATOR);
            lv_obj_set_style_radius(_cards[i].bar_prog, 7, 0);
            lv_obj_set_style_radius(_cards[i].bar_prog, 7, LV_PART_INDICATOR);
            lv_bar_set_range(_cards[i].bar_prog, 0, 100);
            lv_obj_align(_cards[i].bar_prog, LV_ALIGN_BOTTOM_LEFT, 4, -4);

            _cards[i].lbl_prog = lv_label_create(_cards[i].root);
            lv_obj_set_style_text_font(_cards[i].lbl_prog, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(_cards[i].lbl_prog, COL_ACC, 0);
            lv_label_set_text(_cards[i].lbl_prog, "0%");
            lv_obj_align(_cards[i].lbl_prog, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
        } else {
            // Reposition existing card
            int ca_w = cw - 16;
            int job_w = ca_w - 8;
            lv_obj_set_pos(_cards[i].root, x, y);
            lv_obj_set_size(_cards[i].root, cw, ch);
            lv_obj_set_width(_cards[i].bar_prog, ca_w - 80);
            lv_obj_set_width(_cards[i].lbl_job, job_w);
        }
    }

    // Hide unused cards
    for (int i = _num_printers; i < MAX_PRINTERS; i++) {
        if (_cards[i].root) {
            lv_obj_add_flag(_cards[i].root, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ─────────────────────────────────────────────────────────────
void ScreenOverview::updateCard(int idx, const PrinterStatus &s) {
    if (idx < 0 || idx >= _num_printers || !_cards[idx].root) return;

    char buf[64];

    // State badge
    lv_label_set_text(_cards[idx].lbl_state, s.state_str);

    lv_label_set_text(_cards[idx].lbl_ip, strlen(s.name) ? s.name : s.ip);

    // Job name
    if (strlen(s.job_name) > 0)
        lv_label_set_text(_cards[idx].lbl_job, s.job_name);
    else
        lv_label_set_text(_cards[idx].lbl_job, "No active print");

    // Progress
    lv_bar_set_value(_cards[idx].bar_prog, s.progress, LV_ANIM_ON);
    snprintf(buf, sizeof(buf), "%u%%", s.progress);
    lv_label_set_text(_cards[idx].lbl_prog, buf);

    // Remaining time (always shown)
    {
        uint16_t h = s.remaining_min / 60;
        uint16_t m = s.remaining_min % 60;
        if (h > 0)
            snprintf(buf, sizeof(buf), LV_SYMBOL_NEXT " %uh %02um", h, m);
        else
            snprintf(buf, sizeof(buf), LV_SYMBOL_NEXT " %um", m);
        lv_label_set_text(_cards[idx].lbl_remain, buf);
    }

    // Temps
    snprintf(buf, sizeof(buf), LV_SYMBOL_DOWNLOAD " %.0f°C", s.temp_nozzle);
    lv_label_set_text(_cards[idx].lbl_nozzle, buf);
    snprintf(buf, sizeof(buf), LV_SYMBOL_UPLOAD " %.0f°C", s.temp_bed);
    lv_label_set_text(_cards[idx].lbl_bed, buf);
    snprintf(buf, sizeof(buf), LV_SYMBOL_HOME " %.0f°C", s.temp_chamber);
    lv_label_set_text(_cards[idx].lbl_chamber, buf);

    // Badge colour based on state
    lv_color_t c;
    switch (s.state) {
        case PrintState::RUNNING:
        case PrintState::PREPARE: c = COL_ACC;  break;
        case PrintState::PAUSE:   c = COL_ORANGE; break;
        case PrintState::FAILED:  c = COL_RED;   break;
        default:                  c = COL_GREY;  break;
    }
    lv_obj_set_style_text_color(_cards[idx].lbl_state, c, 0);
}

// ─────────────────────────────────────────────────────────────
void ScreenOverview::setThumbnail(int idx, const lv_img_dsc_t *dsc) {
    if (idx < 0 || idx >= MAX_PRINTERS || !_cards[idx].img_thumb || !dsc) return;
    int sw = dsc->header.w, sh = dsc->header.h;
    int dw = 100, dh = 100;
    if (!_cards[idx].thumb_buf) {
        _cards[idx].thumb_buf = (uint8_t *)ps_malloc(dw * dh * 2);
        if (!_cards[idx].thumb_buf) return;
        _cards[idx].thumb_dsc.header.w   = dw;
        _cards[idx].thumb_dsc.header.h   = dh;
        _cards[idx].thumb_dsc.header.cf  = LV_IMG_CF_TRUE_COLOR;
        _cards[idx].thumb_dsc.data_size  = dw * dh * 2;
        _cards[idx].thumb_dsc.data       = _cards[idx].thumb_buf;
    }
    // Nearest-neighbour scale
    const uint8_t *src = (const uint8_t *)dsc->data;
    uint8_t *dst = _cards[idx].thumb_buf;
    for (int oy = 0; oy < dh; oy++) {
        int sy = (oy * sh + dh / 2) / dh;
        for (int ox = 0; ox < dw; ox++) {
            int sx = (ox * sw + dw / 2) / dw;
            int si = (sy * sw + sx) * 2;
            int oi = (oy * dw + ox) * 2;
            dst[oi]     = src[si];
            dst[oi + 1] = src[si + 1];
        }
    }
    lv_img_set_src(_cards[idx].img_thumb, &_cards[idx].thumb_dsc);
    lv_obj_invalidate(_cards[idx].img_thumb);
}

// ─────────────────────────────────────────────────────────────
void ScreenOverview::_card_static_cb(lv_event_t *e) {
    auto *self = (ScreenOverview *)lv_event_get_user_data(e);
    lv_obj_t *target = lv_event_get_target(e);
    // Find which card was tapped
    for (int i = 0; i < self->_num_printers; i++) {
        if (self->_cards[i].root == target) {
            if (self->_tap_cb) self->_tap_cb(i);
            return;
        }
    }
}
