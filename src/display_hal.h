#pragma once
// Display hardware under status_display_lvgl.cpp: panel, backlight, touch and
// the Wake button. One implementation per TFT board, chosen by build_src_filter
// in platformio.ini: display_hal_heltec_exp.cpp, display_hal_wio_l2.cpp.
#include <stddef.h>
#include <stdint.h>

bool dhalBegin(char *err, size_t cap);                                   // panel + backlight + touch; false = display unusable, reason in err
void dhalFlush(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *px);   // RGB565
bool dhalTouch(int16_t &x, int16_t &y);                                  // false = released, or touch is off
void dhalBacklight(uint8_t level);
void dhalOff();                                                          // display failed after begin: backlight 0 if the panel is up
bool dhalWakePressed();                                                  // one true per press; Heltec: always false
