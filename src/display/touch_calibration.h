#pragma once
/**
 * touch_calibration.h  –  Raw GT911 calibration wizard
 *
 * The GT911 is a capacitive controller that reports raw 12-bit (0–4095)
 * coordinates, but the axes, origin, and scale may not match the display.
 * This wizard shows 4 corner crosshairs, reads the raw 12-bit values at
 * each corner, computes a linear mapping (including X/Y swap detection),
 * and persists it to NVS so _gt911_get_touch() can apply it on every boot.
 *
 * Calibration is triggered:
 *   • Automatically on first boot (no NVS data found)
 *   • Manually via the "Calibrate Touchscreen" button on the WiFi configuration screen
 *
 * Public API
 * ──────────
 *   touch_cal_has()          → true if valid calibration exists in NVS
 *   touch_cal_load()         → load NVS data into s_tcal; call at every boot
 *   touch_cal_run(lcd)       → blocking wizard; uses LGFX direct draw
 *   touch_cal_clear()        → erase NVS data (forces re-cal on next boot)
 */

#include <Preferences.h>
#include "display_driver.h"   // LGFX / lcd / s_tcal / _gt911_get_raw()

static const char    *TOUCH_CAL_NS      = "bambu_mon";
static const char    *TOUCH_CAL_VER_KEY = "touch_ver";
static const uint8_t  TOUCH_CAL_VERSION = 6;   // bump invalidates old data

// ── NVS helpers ───────────────────────────────────────────────

inline bool touch_cal_has() {
    Preferences p;
    p.begin(TOUCH_CAL_NS, true);
    bool ok = p.isKey(TOUCH_CAL_VER_KEY) &&
              p.getUChar(TOUCH_CAL_VER_KEY, 0) == TOUCH_CAL_VERSION &&
              p.isKey("tcal_xl");
    p.end();
    return ok;
}

inline void touch_cal_load() {
    if (!touch_cal_has()) return;
    Preferences p;
    p.begin(TOUCH_CAL_NS, true);
    s_tcal.raw_xl  = p.getInt("tcal_xl", 0);
    s_tcal.raw_xr  = p.getInt("tcal_xr", 4095);
    s_tcal.raw_yt  = p.getInt("tcal_yt", 0);
    s_tcal.raw_yb  = p.getInt("tcal_yb", 4095);
    s_tcal.swap_xy = p.getBool("tcal_sw", false);
    s_tcal.valid   = true;
    p.end();
    log_i("Touch cal loaded: xl=%d xr=%d yt=%d yb=%d swap=%d",
          (int)s_tcal.raw_xl, (int)s_tcal.raw_xr,
          (int)s_tcal.raw_yt, (int)s_tcal.raw_yb,
          (int)s_tcal.swap_xy);
}

inline void touch_cal_clear() {
    Preferences p;
    p.begin(TOUCH_CAL_NS, false);
    p.remove("tcal_xl"); p.remove("tcal_xr");
    p.remove("tcal_yt"); p.remove("tcal_yb");
    p.remove("tcal_sw"); p.remove(TOUCH_CAL_VER_KEY);
    p.end();
    s_tcal.valid = false;
}

// ── Internal wizard helpers ───────────────────────────────────

namespace _tcal {

// Draw a crosshair + circle target at (cx, cy) in the given colour.
static void _draw_target(LGFX &lcd, uint16_t cx, uint16_t cy, uint32_t col) {
    const uint16_t R = 24, ARM = 40;
    lcd.drawCircle(cx, cy, R,     col);
    lcd.drawCircle(cx, cy, R - 1, col);           // 2-px thick ring
    lcd.drawLine(cx - ARM, cy, cx + ARM, cy, col);
    lcd.drawLine(cx, cy - ARM, cx, cy + ARM, col);
}

// Clear the background and draw an instruction screen, then place target.
static void _draw_step(LGFX &lcd, int step,
                        const char *corner_name,
                        uint16_t tx, uint16_t ty) {
    lcd.fillScreen(0x1F1F1FUL);                   // dark grey

    // ── Title bar ────────────────────────────────────────────
    lcd.fillRect(0, 0, LCD_WIDTH, 56, 0x0F3460UL);
    lcd.setTextColor(0xFFFFFFUL, 0x0F3460UL);
    lcd.setTextSize(2);
    lcd.setCursor(20, 16);
    lcd.printf("Touch Calibration  (%d / 4)", step + 1);

    // ── Instruction ───────────────────────────────────────────
    lcd.setTextColor(0xCCCCCCUL, 0x1F1F1FUL);
    lcd.setTextSize(2);
    lcd.setCursor(20, 80);
    lcd.printf("Tap the  %s  crosshair", corner_name);
    lcd.setTextSize(1);
    lcd.setCursor(20, 114);
    lcd.print("Hold until the ring turns green, then lift.");

    // ── Target ────────────────────────────────────────────────
    _draw_target(lcd, tx, ty, 0xFFFFFFUL);
}

// Wait for stable raw touch (debounce).
// Returns raw 12-bit (x, y) averaged over several frames.
// Uses a simple state machine:  IDLE → TOUCHING → STABLE → done.
static void _wait_tap(uint16_t *rx, uint16_t *ry,
                      LGFX &lcd, uint16_t tx, uint16_t ty) {
    // Phase 1: ensure no finger is currently down
    uint32_t t0 = millis();
    while (millis() - t0 < 800) {
        uint16_t dummy_x, dummy_y;
        _gt911_get_raw(&dummy_x, &dummy_y);
        delay(20);
    }

    // Phase 2: wait for touch, accumulate samples
    int32_t sum_x = 0, sum_y = 0;
    int     n     = 0;
    bool    confirmed = false;

    while (!confirmed) {
        uint16_t bx, by;
        if (_gt911_get_raw(&bx, &by)) {
            sum_x += bx;
            sum_y += by;
            n++;
            if (n >= 6) {
                // Enough samples — accept this tap
                *rx = (uint16_t)(sum_x / n);
                *ry = (uint16_t)(sum_y / n);
                confirmed = true;
                // Flash target green
                _draw_target(lcd, tx, ty, 0x00FF00UL);
            }
        } else {
            // Finger lifted before we had enough samples — reset
            sum_x = 0; sum_y = 0; n = 0;
        }
        delay(30);
    }

    // Phase 3: wait for finger lift.
    // _gt911_wait_lift() blocks until GT911 reports count==0 in a fresh
    // ready-sample — NOT just "buffer not ready" after we cleared 0x814E.
    _gt911_wait_lift(3000);
}

} // namespace _tcal

// ── Public calibration wizard ─────────────────────────────────

/**
 * Run the full 4-corner calibration wizard.
 *
 * Call BEFORE starting LVGL tasks (uses LGFX direct draw, polls GT911 raw).
 * Blocks until all four corners have been tapped and data is saved to NVS.
 */
inline void touch_cal_run(LGFX &lcd) {
    // Invalidate any in-memory cal so _gt911_get_touch() does raw pass-through
    // while the wizard is running (targets are drawn in raw-coord space).
    s_tcal.valid = false;

    const uint16_t W = LCD_WIDTH, H = LCD_HEIGHT;
    const uint16_t M = 60;   // margin from each edge in screen pixels

    // Target screen positions (order: TL, TR, BR, BL)
    struct { uint16_t sx, sy; const char *name; } tgts[4] = {
        { M,     M,     "TOP-LEFT"     },
        { W - M, M,     "TOP-RIGHT"    },
        { W - M, H - M, "BOTTOM-RIGHT" },
        { M,     H - M, "BOTTOM-LEFT"  },
    };

    uint16_t raw_x[4] = {}, raw_y[4] = {};

    for (int i = 0; i < 4; i++) {
        _tcal::_draw_step(lcd, i, tgts[i].name, tgts[i].sx, tgts[i].sy);
        _tcal::_wait_tap(&raw_x[i], &raw_y[i],
                         lcd, tgts[i].sx, tgts[i].sy);
        log_i("Cal step %d (%s): raw=(%u, %u)",
              i, tgts[i].name, raw_x[i], raw_y[i]);
    }

    // ── Detect X/Y axis swap ──────────────────────────────────
    // Strategy: use ALL four corners rather than just TL→TR (which can
    // give ambiguous results when dx ≈ dy).
    //
    // Horizontal movements (TL→TR top row, BL→BR bottom row):
    //   If axes are NOT swapped, GT911-X should change a lot and GT911-Y
    //   should stay roughly constant.
    // Vertical movements (TL→BL left col, TR→BR right col):
    //   If axes are NOT swapped, GT911-Y should change a lot and GT911-X
    //   should stay roughly constant.
    //
    // Sum both horizontal and vertical contributions for a robust vote.
    int32_t H_dx = abs((int32_t)raw_x[1]-raw_x[0]) + abs((int32_t)raw_x[2]-raw_x[3]); // horiz X change
    int32_t H_dy = abs((int32_t)raw_y[1]-raw_y[0]) + abs((int32_t)raw_y[2]-raw_y[3]); // horiz Y change
    int32_t V_dx = abs((int32_t)raw_x[3]-raw_x[0]) + abs((int32_t)raw_x[2]-raw_x[1]); // vert  X change
    int32_t V_dy = abs((int32_t)raw_y[3]-raw_y[0]) + abs((int32_t)raw_y[2]-raw_y[1]); // vert  Y change
    // No-swap score: horiz → big X change + vert → big Y change
    int32_t score_no_swap = H_dx + V_dy;
    // Swap score: horiz → big Y change + vert → big X change
    int32_t score_swap    = H_dy + V_dx;
    bool swap_xy = (score_swap > score_no_swap);
    log_i("Cal: H_dx=%d H_dy=%d V_dx=%d V_dy=%d → swap_xy=%d",
          (int)H_dx,(int)H_dy,(int)V_dx,(int)V_dy,(int)swap_xy);

    if (swap_xy) {
        for (int i = 0; i < 4; i++) {
            uint16_t t = raw_x[i]; raw_x[i] = raw_y[i]; raw_y[i] = t;
        }
    }

    // ── Compute linear mapping ────────────────────────────────
    // Average symmetric pairs to reduce per-tap error:
    //   left  = avg(TL.x, BL.x)    right = avg(TR.x, BR.x)
    //   top   = avg(TL.y, TR.y)    bottom= avg(BL.y, BR.y)
    int32_t left_raw  = ((int32_t)raw_x[0] + raw_x[3]) / 2;
    int32_t right_raw = ((int32_t)raw_x[1] + raw_x[2]) / 2;
    int32_t top_raw   = ((int32_t)raw_y[0] + raw_y[1]) / 2;
    int32_t bot_raw   = ((int32_t)raw_y[2] + raw_y[3]) / 2;

    // Targets are at screen pixels M…W-M, so extrapolate to full 0…W-1 range.
    // raw_per_pixel_x = (right_raw - left_raw) / (W - 2*M)
    // raw_xl (screen x=0)   = left_raw  - raw_per_pixel_x * M
    // raw_xr (screen x=W-1) = right_raw + raw_per_pixel_x * M
    int32_t rspan_x = right_raw - left_raw;
    int32_t sspan_x = (int32_t)W - 2 * M;
    int32_t raw_xl  = left_raw  - (int64_t)rspan_x * M / sspan_x;
    int32_t raw_xr  = right_raw + (int64_t)rspan_x * M / sspan_x;

    int32_t rspan_y = bot_raw - top_raw;
    int32_t sspan_y = (int32_t)H - 2 * M;
    int32_t raw_yt  = top_raw - (int64_t)rspan_y * M / sspan_y;
    int32_t raw_yb  = bot_raw + (int64_t)rspan_y * M / sspan_y;

    log_i("Cal result: xl=%d xr=%d yt=%d yb=%d swap=%d",
          (int)raw_xl, (int)raw_xr, (int)raw_yt, (int)raw_yb, (int)swap_xy);

    // ── Apply to runtime struct ───────────────────────────────
    s_tcal.raw_xl  = raw_xl;
    s_tcal.raw_xr  = raw_xr;
    s_tcal.raw_yt  = raw_yt;
    s_tcal.raw_yb  = raw_yb;
    s_tcal.swap_xy = swap_xy;
    s_tcal.valid   = true;

    // ── Persist to NVS ────────────────────────────────────────
    Preferences p;
    p.begin(TOUCH_CAL_NS, false);
    p.putInt ("tcal_xl",         raw_xl);
    p.putInt ("tcal_xr",         raw_xr);
    p.putInt ("tcal_yt",         raw_yt);
    p.putInt ("tcal_yb",         raw_yb);
    p.putBool("tcal_sw",         swap_xy);
    p.putUChar(TOUCH_CAL_VER_KEY, TOUCH_CAL_VERSION);
    p.end();

    // ── Confirmation screen ───────────────────────────────────
    lcd.fillScreen(0x0A2647UL);
    lcd.setTextColor(0x00FF88UL, 0x0A2647UL);
    lcd.setTextSize(3);
    lcd.setCursor(20, 40);
    lcd.print("Calibration saved!");

    lcd.setTextColor(0xCCCCCCUL, 0x0A2647UL);
    lcd.setTextSize(1);
    int y = 110;
    lcd.setCursor(20, y);       lcd.printf("swap XY : %s",    swap_xy ? "YES" : "NO");  y += 22;
    lcd.setCursor(20, y);       lcd.printf("raw X range : %d  ..  %d", (int)raw_xl, (int)raw_xr); y += 22;
    lcd.setCursor(20, y);       lcd.printf("raw Y range : %d  ..  %d", (int)raw_yt, (int)raw_yb); y += 34;
    lcd.setCursor(20, y);       lcd.print("Raw corner values (after swap correction):");           y += 22;
    lcd.setCursor(20, y);       lcd.printf("  TL (%d, %d)   TR (%d, %d)",
                                            (int)raw_x[0], (int)raw_y[0],
                                            (int)raw_x[1], (int)raw_y[1]);               y += 22;
    lcd.setCursor(20, y);       lcd.printf("  BR (%d, %d)   BL (%d, %d)",
                                            (int)raw_x[2], (int)raw_y[2],
                                            (int)raw_x[3], (int)raw_y[3]);

    delay(4000);
}
