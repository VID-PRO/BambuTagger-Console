/**
 * lv_conf.h  –  LVGL 9.5 configuration for ESP32-8048S043 (800×480)
 */
#if 1

#ifndef LV_CONF_H
#define LV_CONF_H

#if !defined(__ASSEMBLY__)
#include <stdint.h>
#endif

/* ── Color depth ───────────────────────────────────────── */
#define LV_COLOR_DEPTH 16

/* ── OS (none — we call lv_tick_inc+lv_timer_handler manually) ─ */
#define LV_USE_OS LV_OS_NONE

/* ── Memory / stdlib (PSRAM via ps_malloc) ─────────────── */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CUSTOM
#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM
  #define LV_STDLIB_MALLOC_CUSTOM_INCLUDE <stdlib.h>
  #define LV_STDLIB_MALLOC_CUSTOM_ALLOC   ps_malloc
  #define LV_STDLIB_MALLOC_CUSTOM_FREE    free
  #define LV_STDLIB_MALLOC_CUSTOM_REALLOC ps_realloc
#endif

#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_STDINT_INCLUDE       <stdint.h>
#define LV_STDDEF_INCLUDE       <stddef.h>
#define LV_STDBOOL_INCLUDE      <stdbool.h>
#define LV_INTTYPES_INCLUDE     <inttypes.h>
#define LV_LIMITS_INCLUDE       <limits.h>
#define LV_STDARG_INCLUDE       <stdarg.h>

/* ── Logging ───────────────────────────────────────────── */
#define LV_USE_LOG 1
#if LV_USE_LOG
  #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
  #define LV_LOG_PRINTF 1
#endif

/* ── HAL: refresh period, DPI ──────────────────────────── */
#define LV_DEF_REFR_PERIOD 33
#define LV_DPI_DEF 130

/* ── Rendering: SW only, 40-line partial buffers ───────── */
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_DRAW_UNIT_CNT 1
#define LV_DRAW_SW_SUPPORT_RGB565       1
#define LV_DRAW_SW_SUPPORT_RGB565A8     1
#define LV_USE_NATIVE_HELIUM_ASM        0

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

/* ── Theme ─────────────────────────────────────────────── */
#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
  #define LV_THEME_DEFAULT_DARK 1
  #define LV_THEME_DEFAULT_GROW 1
  #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO   0

/* ── Widgets ───────────────────────────────────────────── */
#define LV_USE_ANIMIMG     1
#define LV_USE_ARC         1
#define LV_USE_BAR         1
#define LV_USE_BUTTON      1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CANVAS      1
#define LV_USE_CHECKBOX    1
#define LV_USE_DROPDOWN    1
#define LV_USE_IMAGE       1
#define LV_USE_KEYBOARD    1
#define LV_USE_LABEL       1
#define LV_USE_LINE        1
#define LV_USE_LIST        1
#define LV_USE_MSGBOX      1
#define LV_USE_ROLLER      1
#define LV_USE_SLIDER      1
#define LV_USE_SPAN        1
#define LV_USE_SPINBOX     1
#define LV_USE_SWITCH      1
#define LV_USE_TABLE       1
#define LV_USE_TABVIEW     1
#define LV_USE_TEXTAREA    1
#define LV_USE_TILEVIEW    1
#define LV_USE_WIN         1

/* ── Layouts ───────────────────────────────────────────── */
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/* ── Misc ──────────────────────────────────────────────── */
#define LV_USE_OBSERVER 1
#define LV_USE_SYSMON   0

/* ── Assertions ────────────────────────────────────────── */
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

#define LV_ASSERT_HANDLER_INCLUDE <stdint.h>
#define LV_ASSERT_HANDLER while(1);

#endif
#endif
