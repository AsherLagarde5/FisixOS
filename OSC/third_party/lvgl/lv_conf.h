/* FisixOS lv_conf.h — LVGL v9.3.0 configuration.
 *
 * Freestanding x86_64 kernel, UEFI GOP framebuffer, 32bpp BGRX
 * (bytes in memory B,G,R,X; a colour as uint32_t is (R<<16)|(G<<8)|B).
 * No libc, no kernel malloc: LVGL uses its own TLSF allocator on a
 * static 1 MiB pool (LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN).
 * No filesystem, no GPU, no OS (LV_OS_NONE), software renderer only.
 * Only the widgets the Win8 Start screen uses are enabled, so the
 * vendored subset in third_party/lvgl/src stays small.
 */
#ifndef LV_CONF_H
#define LV_CONF_H
#ifndef FISIX_LV_CONF_H
#define FISIX_LV_CONF_H

#define LV_COLOR_DEPTH 32

#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN

#define LV_STDINT_INCLUDE       <stdint.h>
#define LV_STDDEF_INCLUDE       <stddef.h>
#define LV_STDBOOL_INCLUDE      <stdbool.h>
#define LV_INTTYPES_INCLUDE     <inttypes.h>
#define LV_LIMITS_INCLUDE       <limits.h>
#define LV_STDARG_INCLUDE       <stdarg.h>

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    #define LV_MEM_SIZE (1024 * 1024U)
    #define LV_MEM_POOL_EXPAND_SIZE 0
    #define LV_MEM_ADR 0
    #if LV_MEM_ADR == 0
        #undef LV_MEM_POOL_INCLUDE
        #undef LV_MEM_POOL_ALLOC
    #endif
#endif

#define LV_DEF_REFR_PERIOD  33
#define LV_DPI_DEF 130
#define LV_USE_OS   LV_OS_NONE

#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_SW_SUPPORT_RGB888       1
#define LV_DRAW_SW_SUPPORT_XRGB8888     1
#define LV_DRAW_SW_SUPPORT_ARGB8888     1

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0

/* Animation is core in LVGL v9 (lv_anim), always built in. */
#define LV_USE_ANIMATION 1

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_FONT_FMT_TXT_LARGE 0
#define LV_USE_FONT_COMPRESSED 0

#define LV_USE_BIDI 0

/* Widgets: only what the Win8 Start screen uses. Everything else
 * is off, because its .c was not vendored. */
#define LV_USE_ANIMIMG    0
#define LV_USE_ARC        0
#define LV_USE_BAR        1
#define LV_USE_BUTTON     1
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR   0
#define LV_USE_CANVAS     0
#define LV_USE_CHART      0
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   0
#define LV_USE_IMAGE      1
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD   0
#define LV_USE_LABEL      1
#define LV_USE_LED        0
#define LV_USE_LINE       0
#define LV_USE_LIST       0
#define LV_USE_LOTTIE     0
#define LV_USE_MENU       0
#define LV_USE_MSGBOX     0
#define LV_USE_ROLLER     0
#define LV_USE_SCALE      0
#define LV_USE_SLIDER     0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0
#define LV_USE_SWITCH     0
#define LV_USE_TABLE      0
#define LV_USE_TABVIEW    0
#define LV_USE_TEXTAREA   1
#define LV_USE_TILEVIEW   1
#define LV_USE_WIN        0
#define LV_USE_3DTEXTURE  0

#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 0
    #define LV_THEME_DEFAULT_GROW 1
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0
#define LV_USE_FS_MEMFS 0
#define LV_USE_FS_LITTLEFS 0
#define LV_USE_FS_ARDUINO_ESP_LITTLEFS 0
#define LV_USE_FS_ARDUINO_SD 0
#define LV_USE_FS_UEFI 0

#define LV_USE_SNAPSHOT 0
#define LV_USE_SYSMON 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#endif /* FISIX_LV_CONF_H */
#endif /* LV_CONF_H */
