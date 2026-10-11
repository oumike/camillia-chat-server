// Display hardware for the LilyGo T-LoRa Pager (tlora-pager-tft): ST7796
// through LovyanGFX on the bus it shares with the radio, PWM backlight, no
// touch, and the rotary wheel press as the Wake button (camillia-mt 3777e74:
// src/keyboard.cpp reads TBALL_CLICK active LOW; src/main_lvgl.cpp lists it
// among the screen-wake lines).
#include "display_hal.h"
#include <Arduino.h>
#include <stdio.h>
#include "board.h"
#include "tlora_pager_display.h"
#include "wake_edge.h"

namespace {

LGFX_TLoraPager s_lcd;
bool            s_panelUp = false;   // lcd.init() succeeded; dhalOff() may then blank the backlight
WakeEdge        s_wake;

}  // namespace

bool dhalBegin(char *err, size_t cap) {
    // The other chip selects are already parked from boardEarlyInit().
    if (!s_lcd.init()) {
        snprintf(err, cap, "panel init failed");
        return false;
    }
    s_panelUp = true;
    s_lcd.setRotation(TFT_ROTATION_LANDSCAPE);
    s_lcd.setBrightness(TFT_BRIGHTNESS_DEFAULT);
    s_lcd.fillScreen(TFT_BLACK);
    return true;
}

void dhalFlush(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *px) {
    s_lcd.pushImage(x, y, w, h, (const lgfx::rgb565_t *)px);
}

bool dhalTouch(int16_t &, int16_t &) { return false; }   // no touch panel

void dhalBacklight(uint8_t level) { s_lcd.setBrightness(level); }

void dhalOff() {
    if (s_panelUp) s_lcd.setBrightness(0);   // don't leave a lit black panel
}

bool dhalWakePressed() {
    return s_wake.update(digitalRead(WAKE_BUTTON_PIN) == LOW, millis());
}
