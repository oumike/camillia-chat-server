// LVGL 9.5.0 config for the heltec-v4-expansion env (found through
// -DLV_CONF_INCLUDE_SIMPLE -Isrc). Trimmed from camillia-mt 32fea4f src/lv_conf.h.
#ifndef LV_CONF_H
#define LV_CONF_H

// Partial config: lv_conf_internal.h fills in the LVGL v9 defaults for every
// option not set here, so only the deltas from stock live in this file.

#define LV_COLOR_DEPTH 16

// Built-in allocator with its fixed pool, the pool itself in PSRAM so it does
// not take internal DRAM from WiFi/TLS (camillia-mt's reasoning, same values
// minus the size). Nothing is created or deleted after displayBegin() apart
// from the splash, so 128 KB is ample.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_POOL_INCLUDE "esp_heap_caps.h"
#define LV_MEM_POOL_ALLOC(size) heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_MEM_SIZE (128U * 1024U)

// Keep LVGL assert-integrity checks off (camillia-mt).
#define LV_USE_ASSERT_MEM_INTEGRITY 0

#define LV_USE_LOG 0

// Off where camillia-mt turns them on; stock default is off too.
#define LV_USE_LODEPNG 0
#define LV_USE_TINY_TTF 0

// Widgets: only the base object (always built), label, and canvas (the boot
// splash). lv_canvas is an lv_image subclass and lv_image needs lv_label, so
// those three stay on; everything else is off. Disabling a widget whose
// dependants were still on would trip their #error checks, which is why the
// whole list is stated rather than just camillia-mt's BAR/CHART/SCALE.
#define LV_USE_LABEL 1
#define LV_USE_IMAGE 1
#define LV_USE_CANVAS 1

#define LV_USE_ANIMIMG 0
#define LV_USE_ARC 0
#define LV_USE_ARCLABEL 0
#define LV_USE_BAR 0
#define LV_USE_BUTTON 0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LINE 0
#define LV_USE_LIST 0
#define LV_USE_LOTTIE 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_SWITCH 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

// LVGL's own Montserrat copies (ASCII plus the symbol glyphs, degree and
// bullet). camillia-mt swaps in Latin-1 cuts; this build does not.
#define LV_FONT_MONTSERRAT_8 0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
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

#endif
