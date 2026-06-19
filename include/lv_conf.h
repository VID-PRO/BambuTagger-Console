/**
 * lv_conf.h  –  LVGL 8.3 configuration for ESP32-8048S043 (800×480)
 */
#if 1  /* Enable this file */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ── Color depth ───────────────────────────────────────── */
#define LV_COLOR_DEPTH 16          /* RGB565 */
#define LV_COLOR_16_SWAP 0

/* ── Memory ────────────────────────────────────────────── */
/* Use custom malloc/free (we route to PSRAM via ps_malloc) */
#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM
  #define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
  #define LV_MEM_CUSTOM_ALLOC   ps_malloc
  #define LV_MEM_CUSTOM_FREE    free
  #define LV_MEM_CUSTOM_REALLOC ps_realloc
#endif

/* ── Logging ───────────────────────────────────────────── */
#define LV_USE_LOG 1
#if LV_USE_LOG
  #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
  #define LV_LOG_PRINTF 1
#endif

/* ── Screen resolution ────────────────────────────────── */
#define LV_HOR_RES_MAX 800
#define LV_VER_RES_MAX 480

/* ── Tick ──────────────────────────────────────────────── */
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
  #define LV_TICK_CUSTOM_INCLUDE <Arduino.h>
  #define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

/* ── Draw buffer ───────────────────────────────────────── */
#define LV_DISP_DEF_REFR_PERIOD 16   /* ~60 fps */
#define LV_INDEV_DEF_READ_PERIOD 30

/* ── Fonts ─────────────────────────────────────────────── */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* ── Symbol / icon fonts ───────────────────────────────── */
#define LV_USE_FONT_SUBPX 1

/* ── Theme ─────────────────────────────────────────────── */
#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
  #define LV_THEME_DEFAULT_DARK 1     /* dark base */
  #define LV_THEME_DEFAULT_GROW 1
  #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif
#define LV_USE_THEME_BASIC 1
#define LV_USE_THEME_MONO  0

/* ── Widgets ───────────────────────────────────────────── */
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1
#define LV_USE_CANVAS     1
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     1
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TABLE      1
#define LV_USE_TEXTAREA   1
#define LV_USE_KEYBOARD   1
#define LV_USE_LIST       1
#define LV_USE_MSGBOX     1
#define LV_USE_TABVIEW    1
#define LV_USE_TILEVIEW   1
#define LV_USE_WIN        1
#define LV_USE_SPAN       1
#define LV_USE_SPINBOX    1
#define LV_USE_METER      1

/* ── Image decoders ────────────────────────────────────── */
#define LV_USE_PNG  1        /* PNG decoder (for thumbnails) */
#define LV_USE_BMP  0
#define LV_USE_SJPG 0        /* We decode JPEG manually via TJpgDec */
#define LV_USE_GIF  0
#define LV_USE_QRCODE 0

/* ── Animation ─────────────────────────────────────────── */
#define LV_USE_ANIMATION 1

/* ── Misc ──────────────────────────────────────────────── */
#define LV_USE_PERF_MONITOR 0     /* set 1 to show FPS overlay */
#define LV_USE_MEM_MONITOR  0
#define LV_USE_REFR_DEBUG   0
#define LV_USE_ASSERT_NULL         1
#define LV_USE_ASSERT_MALLOC       1
#define LV_USE_ASSERT_STYLE        0
#define LV_USE_ASSERT_OBJ          0
#define LV_USE_ASSERT_MEM_INTEGRITY 0

#define LV_SPRINTF_CUSTOM 0
#define LV_USE_USER_DATA  1
#define LV_ENABLE_GC      0

#endif /* LV_CONF_H */
#endif /* End of "Content enable" */
