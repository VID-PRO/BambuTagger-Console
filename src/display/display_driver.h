#pragma once
/**
 * display_driver.h
 *
 * LovyanGFX board definition + LVGL bridge for the
 * Sunton ESP32-8048S043 (ESP32-S3, 4.3", 800×480 RGB, GT911 touch).
 *
 * Pin map (Sunton schematic v1.0):
 *   RGB B[0..4]:  GPIO  8,  3, 46,  9,  1
 *   RGB G[0..5]:  GPIO  5,  6,  7, 15, 16,  4
 *   RGB R[0..4]:  GPIO 45, 48, 47, 21, 14
 *   PCLK:         GPIO 42
 *   HSYNC:        GPIO 39
 *   VSYNC:        GPIO 41
 *   DE:           GPIO 40
 *   Backlight:    GPIO  2  (PWM)
 *   Touch SDA:    GPIO 19  /  SCL: GPIO 20
 *   Touch INT:    GPIO 18  /  RST: GPIO 38
 *
 * LovyanGFX RGB panel wiring:
 *   - lgfx::Bus_RGB  holds the pin / timing configuration
 *   - lgfx::Panel_RGB holds geometry; receives the bus via setBus()
 * Both types require -DCONFIG_IDF_TARGET_ESP32S3 in build_flags so
 * LovyanGFX's platform detection includes the ESP32-S3 headers.
 */

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// LovyanGFX does NOT auto-include these — they must be pulled in explicitly
// (confirmed from official Sunton ESP32-8048S050/S070 example files)
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

#include <Wire.h>
#include <driver/i2c.h>
#include <lvgl.h>
#include "config.h"

// ── Touch I2C pins ────────────────────────────────────────────
static constexpr int TOUCH_SDA = 19;
static constexpr int TOUCH_SCL = 20;
static constexpr int TOUCH_RST = 38;
static constexpr int TOUCH_INT = 18;

// ── GT911 I2C address / register map ─────────────────────────
static constexpr uint8_t  GT911_ADDR      = 0x5D;
static constexpr uint16_t GT911_REG_PID   = 0x8140;  // product-ID "911\0"
static constexpr uint16_t GT911_REG_CFG   = 0x8047;  // config block start
static constexpr uint16_t GT911_REG_CS    = 0x80FF;  // config checksum
static constexpr uint16_t GT911_REG_FRESH = 0x8100;  // 1 = save & apply config
static constexpr int      GT911_CFG_LEN   = 184;     // 0x8047..0x80FE inclusive

// ── GT911 I2C helpers (Wire-based, used pre-lcd.init only) ────
// Wire buffer on ESP32 is 128 bytes. Max safe payload per transaction:
//   write: 128 - 2 (reg addr) = 126 bytes
//   read:  128 bytes
static constexpr int GT911_CHUNK_WR = 126;
static constexpr int GT911_CHUNK_RD = 128;

// Single-chunk write (len must be ≤ GT911_CHUNK_WR)
static bool _gt911_write(uint16_t reg, const uint8_t *buf, uint8_t len) {
    Wire1.beginTransmission(GT911_ADDR);
    Wire1.write((uint8_t)(reg >> 8));
    Wire1.write((uint8_t)(reg & 0xFF));
    for (uint8_t i = 0; i < len; i++) Wire1.write(buf[i]);
    return Wire1.endTransmission() == 0;
}

// Single-chunk read (len must be ≤ GT911_CHUNK_RD)
static bool _gt911_read(uint16_t reg, uint8_t *buf, uint8_t len) {
    Wire1.beginTransmission(GT911_ADDR);
    Wire1.write((uint8_t)(reg >> 8));
    Wire1.write((uint8_t)(reg & 0xFF));
    if (Wire1.endTransmission(false) != 0) return false;
    uint8_t n = Wire1.requestFrom((uint8_t)GT911_ADDR, len);
    for (uint8_t i = 0; i < n; i++) buf[i] = Wire1.read();
    return n == len;
}

// Multi-chunk read (arbitrary length — splits into ≤ GT911_CHUNK_RD reads)
static bool _gt911_read_n(uint16_t reg, uint8_t *buf, int len) {
    int offset = 0;
    while (offset < len) {
        int chunk = len - offset;
        if (chunk > GT911_CHUNK_RD) chunk = GT911_CHUNK_RD;
        if (!_gt911_read(reg + offset, buf + offset, (uint8_t)chunk)) return false;
        offset += chunk;
    }
    return true;
}

// Multi-chunk write (arbitrary length — splits into ≤ GT911_CHUNK_WR writes)
static bool _gt911_write_n(uint16_t reg, const uint8_t *buf, int len) {
    int offset = 0;
    while (offset < len) {
        int chunk = len - offset;
        if (chunk > GT911_CHUNK_WR) chunk = GT911_CHUNK_WR;
        if (!_gt911_write(reg + offset, buf + offset, (uint8_t)chunk)) return false;
        offset += chunk;
    }
    return true;
}

/**
 * gt911_reset()
 *
 * Performs a hardware reset of the GT911 with INT=LOW so the chip latches
 * I2C address 0x5D, then waits the full 100 ms boot time the chip requires.
 *
 * Must be called BEFORE lcd.init().  LGFX is configured with pin_rst=-1 so
 * it skips its own 5 ms reset (too short — GT911 needs ~100 ms to be ready).
 * Wire1 is released at the end so LGFX can claim the bus.
 */
/**
 * gt911_init()
 *
 * Hard-resets GT911 (INT=LOW → address 0x5D), waits full 100 ms boot time,
 * initialises Wire1, and leaves Wire1 running.  Touch reads are done directly
 * via Wire1 (_gt911_get_touch) — LGFX Touch layer is not used.
 */
static void gt911_init() {
    // ── 1. Hardware reset — INT=LOW selects address 0x5D ─────
    pinMode(TOUCH_RST, OUTPUT);
    pinMode(TOUCH_INT, OUTPUT);
    digitalWrite(TOUCH_INT, LOW);   // LOW before RST rises → address 0x5D
    digitalWrite(TOUCH_RST, LOW);
    delay(20);                       // hold reset ≥10 ms
    digitalWrite(TOUCH_RST, HIGH);  // release reset
    delay(10);                       // INT must stay LOW ≥100 µs after RST↑
    pinMode(TOUCH_INT, INPUT_PULLUP); // pull-up: HIGH=idle, GT911 drives LOW when data ready
    delay(100);                      // GT911 boot / self-test (~80–100 ms)

    // ── 2. Init Wire1, scan bus, verify PID ───────────────────
    Wire1.begin(TOUCH_SDA, TOUCH_SCL);
    Wire1.setClock(400000);

    // I2C bus scan — helps diagnose wrong address or missing chip
    log_i("I2C scan on Wire1 (SDA=%d SCL=%d):", TOUCH_SDA, TOUCH_SCL);
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        Wire1.beginTransmission(addr);
        if (Wire1.endTransmission() == 0) {
            log_i("  found device at 0x%02X", addr);
        }
    }

    uint8_t pid[4] = {};
    bool pid_ok = _gt911_read(GT911_REG_PID, pid, 4);
    log_i("GT911 PID: %c%c%c%c  (ok=%d)  addr=0x%02X",
          pid[0], pid[1], pid[2], pid[3], pid_ok, GT911_ADDR);
    if (!pid_ok) {
        log_e("GT911 not found at 0x%02X — check SDA=%d SCL=%d RST=%d INT=%d",
              GT911_ADDR, TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);
        return;
    }

    // ── 3. Write X/Y resolution so GT911 reports pixel coords ─
    // Read the full 184-byte config block (0x8047–0x80FE), patch
    // X_RES and Y_RES, recompute the 1-byte checksum, then write back.
    // Without this the chip reports raw capacitive values in its own
    // internal coordinate space (~4096×4096) rather than screen pixels.
    const uint16_t CFG_BASE = 0x8047;
    const int      CFG_LEN  = 184;   // 0x8047..0x80FE inclusive
    uint8_t cfg[CFG_LEN] = {};
    if (_gt911_read_n(CFG_BASE, cfg, CFG_LEN)) {
        // Patch resolution: little-endian 16-bit at offsets 1-2 (X) and 3-4 (Y)
        cfg[1] = (uint8_t)(LCD_WIDTH  & 0xFF);
        cfg[2] = (uint8_t)(LCD_WIDTH  >> 8);
        cfg[3] = (uint8_t)(LCD_HEIGHT & 0xFF);
        cfg[4] = (uint8_t)(LCD_HEIGHT >> 8);

        // IRQ output mode: reg 0x8093 = offset 76 in the config block.
        // 0x00=rising-edge  0x01=falling-edge  0x02=low-level  0x03=high-pulse
        // We need LOW-LEVEL so INT stays LOW while buffer_ready (0x814E bit7)
        // is set and goes HIGH only after we clear it — compatible with polling.
        // Default (power-on) is pulse mode, which goes HIGH before our 33ms
        // poll sees it and blocks every touch read.
        cfg[76] = 0x02;   // low-level IRQ — stays LOW until we clear 0x814E

        // Bump config version so GT911 accepts the update
        if (cfg[0] < 0xFF) cfg[0]++;

        // Compute checksum: ~(sum of all cfg bytes) + 1, truncated to uint8_t
        uint8_t sum = 0;
        for (int i = 0; i < CFG_LEN; i++) sum += cfg[i];
        uint8_t chk = (~sum + 1) & 0xFF;

        _gt911_write_n(CFG_BASE, cfg, CFG_LEN);
        _gt911_write(0x80FF, &chk, 1);    // checksum register
        uint8_t fresh = 1;
        _gt911_write(0x8100, &fresh, 1);  // commit / fresh flag
        delay(20);

        // Read back X/Y res + IRQ mode to confirm the chip accepted the config.
        // Expect: X=800, Y=480, irq=2 (low-level)
        uint8_t rb[4] = {};
        _gt911_read_n(CFG_BASE + 1, rb, 4);  // 0x8048..0x804B = X/Y res
        uint16_t rx = rb[0] | (rb[1] << 8);
        uint16_t ry = rb[2] | (rb[3] << 8);
        uint8_t irq_mode = 0;
        _gt911_read(CFG_BASE + 76, &irq_mode, 1);  // 0x8093
        log_i("GT911 config: wrote X=%d Y=%d irq=2  readback X=%d Y=%d irq=%d  chk=0x%02X",
              LCD_WIDTH, LCD_HEIGHT, rx, ry, irq_mode, chk);
    } else {
        log_w("GT911 config read failed — coordinates may be in raw IC space");
    }
    // Wire1 stays open — _gt911_get_touch() uses it every LVGL tick
}

// ── Touch calibration map (populated by touch_calibration.h) ─────────────
// Declared here — before _gt911_get_touch() — so the compiler sees it.
struct TouchCalMap {
    int32_t raw_xl;   // raw 12-bit X value that maps to screen x = 0
    int32_t raw_xr;   // raw 12-bit X value that maps to screen x = LCD_WIDTH-1
    int32_t raw_yt;   // raw 12-bit Y value that maps to screen y = 0
    int32_t raw_yb;   // raw 12-bit Y value that maps to screen y = LCD_HEIGHT-1
    bool    swap_xy;  // true when GT911 X/Y axes are transposed vs display
    bool    valid;    // true once load or run has been called
};
static TouchCalMap s_tcal = {};

/**
 * _gt911_get_touch()
 *
 * Direct GT911 touch read via Wire1 (bypasses LGFX touch layer).
 * Call from LVGL input-device callback only (Core 0, single-threaded).
 *
 * Returns true + sets *x/*y when a finger is down.
 * Clears the GT911 coordinate-ready flag on every call so the chip
 * can queue the next sample.
 */
static bool _gt911_get_touch(uint16_t *x, uint16_t *y) {
    // Poll 0x814E directly — no INT-pin gate.
    // Bit 7: buffer-ready flag  |  bits[3:0]: touch-point count
    // Clearing 0x814E after every read is what allows the chip to queue
    // the next sample (regardless of IRQ output mode).
    uint8_t status = 0;
    if (!_gt911_read(0x814E, &status, 1)) return false;

    // Always clear so chip can queue next sample and release INT HIGH.
    uint8_t zero = 0;
    _gt911_write(0x814E, &zero, 1);

    if (!(status & 0x80)) return false;   // buffer not ready — no new data
    uint8_t npts = status & 0x0F;
    if (npts == 0) return false;          // buffer ready but no fingers

    // Read first touch point (8 bytes at 0x8150)
    // Actual GT911 layout on this board (ESP32-8048S043): low byte FIRST
    //   pt[0] = X_low[7:0]
    //   pt[1] = reserved[7:4] | X_high[3:0]
    //   pt[2] = Y_low[7:0]
    //   pt[3] = reserved[7:4] | Y_high[3:0]
    //   pt[4] = touch area size
    //   pt[5..7] = reserved
    uint8_t pt[8] = {};
    if (!_gt911_read(0x8150, pt, 8)) return false;

    // Log raw bytes once every 2 s so we can verify byte layout without flooding serial
    static uint32_t _last_raw_log = 0;
    if (millis() - _last_raw_log > 2000) {
        _last_raw_log = millis();
        log_i("GT911 raw pt: %02X %02X %02X %02X %02X %02X %02X %02X",
              pt[0],pt[1],pt[2],pt[3],pt[4],pt[5],pt[6],pt[7]);
    }

    // 12-bit coords: low byte in pt[0/2], high nibble in low 4 bits of pt[1/3]
    uint16_t tx = (uint16_t)pt[0] | ((uint16_t)(pt[1] & 0x0F) << 8);
    uint16_t ty = (uint16_t)pt[2] | ((uint16_t)(pt[3] & 0x0F) << 8);

    // ── Apply calibration if available, else fall back to 4096 auto-scale ──
    // (s_tcal is populated by touch_cal_load() / touch_cal_run() at boot)
    if (s_tcal.valid) {
        if (s_tcal.swap_xy) { uint16_t t = tx; tx = ty; ty = t; }
        int32_t sx = (s_tcal.raw_xr == s_tcal.raw_xl) ? 0
                   : ((int32_t)tx - s_tcal.raw_xl) * (LCD_WIDTH - 1)
                     / (s_tcal.raw_xr - s_tcal.raw_xl);
        int32_t sy = (s_tcal.raw_yb == s_tcal.raw_yt) ? 0
                   : ((int32_t)ty - s_tcal.raw_yt) * (LCD_HEIGHT - 1)
                     / (s_tcal.raw_yb - s_tcal.raw_yt);
        if (sx < 0) sx = 0; if (sx >= LCD_WIDTH)  sx = LCD_WIDTH  - 1;
        if (sy < 0) sy = 0; if (sy >= LCD_HEIGHT) sy = LCD_HEIGHT - 1;
        *x = (uint16_t)sx;
        *y = (uint16_t)sy;
    } else {
        // No calibration yet — auto-scale from 12-bit GT911 space.
        if (tx >= LCD_WIDTH || ty >= LCD_HEIGHT) {
            if (tx <= 4095 && ty <= 4095) {
                tx = (uint16_t)((uint32_t)tx * LCD_WIDTH  / 4096);
                ty = (uint16_t)((uint32_t)ty * LCD_HEIGHT / 4096);
            } else {
                log_w("GT911 raw coords out of range: (%u,%u) max=(%d,%d)",
                      tx, ty, LCD_WIDTH-1, LCD_HEIGHT-1);
                return false;
            }
        }
        *x = tx;
        *y = ty;
    }
    return true;
}

// ── Raw GT911 read (12-bit, no scaling or calibration applied) ─
// Used only by the calibration wizard; normal touch reads go through
// _gt911_get_touch() which applies s_tcal.
static bool _gt911_get_raw(uint16_t *x, uint16_t *y) {
    uint8_t status = 0;
    if (!_gt911_read(0x814E, &status, 1)) return false;
    uint8_t zero = 0;
    _gt911_write(0x814E, &zero, 1);
    if (!(status & 0x80)) return false;
    if ((status & 0x0F) == 0) return false;
    uint8_t pt[8] = {};
    if (!_gt911_read(0x8150, pt, 8)) return false;
    // Log all 8 raw bytes every sample during calibration so we can verify layout:
    log_i("GT911 raw bytes: %02X %02X %02X %02X %02X %02X %02X %02X",
          pt[0],pt[1],pt[2],pt[3],pt[4],pt[5],pt[6],pt[7]);
    // This board: low byte first — pt[0]=X_low, pt[1]=X_high(4bit), pt[2]=Y_low, pt[3]=Y_high(4bit)
    *x = (uint16_t)pt[0] | ((uint16_t)(pt[1] & 0x0F) << 8);
    *y = (uint16_t)pt[2] | ((uint16_t)(pt[3] & 0x0F) << 8);
    return true;
}

// Wait until GT911 reports zero touch-points in a genuinely new ready-sample.
// Unlike _gt911_get_raw() which returns false on "not ready" (which happens
// immediately after we clear 0x814E), this function keeps spinning until the
// chip sets the ready-bit again with count==0, confirming the finger is gone.
static void _gt911_wait_lift(unsigned long timeout_ms = 3000) {
    uint32_t deadline = millis() + timeout_ms;
    int zero_count = 0;
    while (millis() < deadline) {
        uint8_t status = 0;
        if (!_gt911_read(0x814E, &status, 1)) { delay(20); continue; }
        if (!(status & 0x80)) { delay(20); continue; }  // not ready yet — keep waiting
        // A fresh ready-sample arrived; check touch count before clearing.
        uint8_t zero = 0;
        _gt911_write(0x814E, &zero, 1);  // consume the sample
        if ((status & 0x0F) == 0) {
            if (++zero_count >= 3) break;  // 3 consecutive zero-touch samples ⇒ lifted
        } else {
            zero_count = 0;  // still touching — reset streak
        }
        delay(20);
    }
    delay(150);   // settling time before next wizard step
}

// ── LGFX board class ─────────────────────────────────────────
// Touch is handled directly via Wire1 + _gt911_get_touch() — not via LGFX.
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_RGB    _panel_instance;
    lgfx::Bus_RGB      _bus_instance;
    lgfx::Light_PWM    _light_instance;

public:
    LGFX() {
        // ── Bus_RGB: pixel clock, sync, and 16-bit RGB pins ──
        {
            auto cfg = _bus_instance.config();

            cfg.panel       = &_panel_instance;

            cfg.freq_write  = 16000000;
            cfg.pin_pclk    = GPIO_NUM_42;
            cfg.pin_hsync   = GPIO_NUM_39;
            cfg.pin_vsync   = GPIO_NUM_41;
            cfg.pin_henable = GPIO_NUM_40;   // DE (data-enable)

            // 16-bit RGB565: d0..d4=B[0..4]  d5..d10=G[0..5]  d11..d15=R[0..4]
            // Correct Sunton ESP32-8048S043 hardware wiring:
            cfg.pin_d0  = GPIO_NUM_8;   // B0
            cfg.pin_d1  = GPIO_NUM_3;   // B1
            cfg.pin_d2  = GPIO_NUM_46;  // B2
            cfg.pin_d3  = GPIO_NUM_9;   // B3
            cfg.pin_d4  = GPIO_NUM_1;   // B4
            cfg.pin_d5  = GPIO_NUM_5;   // G0
            cfg.pin_d6  = GPIO_NUM_6;   // G1
            cfg.pin_d7  = GPIO_NUM_7;   // G2
            cfg.pin_d8  = GPIO_NUM_15;  // G3
            cfg.pin_d9  = GPIO_NUM_16;  // G4
            cfg.pin_d10 = GPIO_NUM_4;   // G5
            cfg.pin_d11 = GPIO_NUM_45;  // R0
            cfg.pin_d12 = GPIO_NUM_48;  // R1
            cfg.pin_d13 = GPIO_NUM_47;  // R2
            cfg.pin_d14 = GPIO_NUM_21;  // R3
            cfg.pin_d15 = GPIO_NUM_14;  // R4

            // Horizontal timing
            cfg.hsync_polarity    = 0;
            cfg.hsync_front_porch = 8;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch  = 16;
            // Vertical timing
            cfg.vsync_polarity    = 0;
            cfg.vsync_front_porch = 4;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch  = 4;

            cfg.pclk_active_neg = 1;   // latch on falling edge
            cfg.de_idle_high    = 0;
            cfg.pclk_idle_high  = 0;

            _bus_instance.config(cfg);
        }

        // ── Panel_RGB: screen geometry ────────────────────────
        {
            auto cfg = _panel_instance.config();
            cfg.memory_width  = LCD_WIDTH;
            cfg.memory_height = LCD_HEIGHT;
            cfg.panel_width   = LCD_WIDTH;
            cfg.panel_height  = LCD_HEIGHT;
            cfg.offset_x      = 0;
            cfg.offset_y      = 0;
            _panel_instance.config(cfg);
        }
        _panel_instance.setBus(&_bus_instance);

        // ── Backlight (PWM on GPIO 2) ─────────────────────────
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl      = LCD_BL_PIN;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        setPanel(&_panel_instance);
    }
};

// ── Global LCD instance ───────────────────────────────────────
extern LGFX lcd;

// ── LVGL draw buffers ─────────────────────────────────────────
// Buffers MUST be in internal DMA-capable SRAM — PSRAM is not DMA-accessible
// on ESP32-S3 without a bounce buffer, causing partial-update tearing/flicker.
// Two buffers of 40 lines each: 800×40×2 = 64 KB total — fits in internal RAM.
static lv_disp_draw_buf_t _draw_buf;
static lv_color_t        *_lvgl_buf1 = nullptr;
static lv_color_t        *_lvgl_buf2 = nullptr;
static constexpr size_t   LVGL_BUF_LINES = 40;

// ── LVGL flush callback ───────────────────────────────────────
// writePixels() is synchronous in LovyanGFX 1.2.21 — pixel data is fully
// committed before we call lv_disp_flush_ready(), so no DMA wait needed.
static void _lvgl_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *buf) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    lcd.startWrite();
    lcd.setAddrWindow(area->x1, area->y1, w, h);
    lcd.writePixels((lgfx::rgb565_t *)buf, w * h);
    lcd.endWrite();
    lv_disp_flush_ready(drv);
}

// ── LVGL touch callback ───────────────────────────────────────
// Reads GT911 directly via Wire1 — no LGFX touch layer involved.
static void _lvgl_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    static uint16_t _last_x = 0, _last_y = 0;
    uint16_t tx = 0, ty = 0;
    bool pressed = _gt911_get_touch(&tx, &ty);
    if (pressed) { _last_x = tx; _last_y = ty; }
    data->state   = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = pressed ? tx : _last_x;
    data->point.y = pressed ? ty : _last_y;

    static uint32_t _last_log  = 0;
    static bool     _last_down = false;
    uint32_t now = millis();
    if (pressed != _last_down || (pressed && now - _last_log > 500)) {
        log_i("TOUCH %s  raw=(%u,%u)", pressed ? "PRESS" : "RELEASE", tx, ty);
        _last_log  = now;
        _last_down = pressed;
    }
}

// ── Initialise display + LVGL ─────────────────────────────────
inline void display_init() {
    // Hard-reset GT911, verify PID, leave Wire1 running.
    // Touch reads bypass LGFX entirely — see _gt911_get_touch().
    gt911_init();

    bool init_ok = lcd.init();
    log_i("lcd.init() = %s", init_ok ? "OK" : "FAILED");
    lcd.setRotation(0);          // rotate 0° (cable-down orientation)
    lcd.setBrightness(200);
    lcd.setColorDepth(16);

    lv_init();

    // Allocate LVGL draw buffers in internal DMA-capable SRAM (not PSRAM).
    // 800×40×2 B = 64 KB total — comfortably fits in ESP32-S3 internal RAM.
    size_t buf_sz = LCD_WIDTH * LVGL_BUF_LINES;
    _lvgl_buf1 = (lv_color_t *)heap_caps_malloc(buf_sz * sizeof(lv_color_t),
                                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    _lvgl_buf2 = (lv_color_t *)heap_caps_malloc(buf_sz * sizeof(lv_color_t),
                                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    assert(_lvgl_buf1 && _lvgl_buf2 && "LVGL DMA buffers failed — not enough internal RAM");
    lv_disp_draw_buf_init(&_draw_buf, _lvgl_buf1, _lvgl_buf2, buf_sz);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = LCD_WIDTH;
    disp_drv.ver_res      = LCD_HEIGHT;
    disp_drv.flush_cb     = _lvgl_flush;
    disp_drv.draw_buf     = &_draw_buf;
    disp_drv.full_refresh = 1;   // always redraw the full screen — eliminates
                                  // partial-update tearing on RGB panels
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = _lvgl_touch_read;
    lv_indev_drv_register(&indev_drv);
}
