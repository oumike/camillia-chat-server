// Display hardware for the LilyGo T-Deck (tdeck): ST7789 through LovyanGFX on
// the bus it shares with the radio, PWM backlight, GT911 touch, and the
// trackball click as the Wake button (camillia-mt 3777e74: src/main_lvgl.cpp
// serviceTdeckTrackballSleepHold reads TBALL_CLICK active LOW).
#include "display_hal.h"
#include <Arduino.h>
#include <stdio.h>
#include "board.h"
#include "tdeck_display.h"
#include "wake_edge.h"

namespace {

constexpr uint32_t kTouchSlowMs = 200;   // a healthy GT911 read takes ~1 ms; a bus timeout far longer

LGFX_TDeckBoard s_lcd;
bool            s_panelUp = false;   // lcd.init() succeeded; dhalOff() may then blank the backlight
bool            s_touchOff = false;
uint8_t         s_touchFails = 0;
WakeEdge        s_wake;

}  // namespace

bool dhalBegin(char *err, size_t cap) {
    // BOARD_POWERON is already HIGH and the other chip selects parked, from boardEarlyInit().
    if (!s_lcd.init()) {
        snprintf(err, cap, "panel init failed");
        return false;
    }
    s_panelUp = true;
    s_lcd.setRotation(TFT_ROTATION_LANDSCAPE);
    s_lcd.setBrightness(TFT_BRIGHTNESS_DEFAULT);
    s_lcd.fillScreen(TFT_BLACK);

    // lcd.init() ignores the GT911 probe result. init() again returns at once
    // when that probe succeeded, otherwise it is one more try.
    if (!s_lcd.touch() || !s_lcd.touch()->init()) {
        s_touchOff = true;
        Serial.printf("[cs] display: touch controller not answering; touch off\n");
    }
    return true;
}

void dhalFlush(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *px) {
    s_lcd.pushImage(x, y, w, h, (const lgfx::rgb565_t *)px);
}

// A failed read is one that took a bus timeout. Three in a row: stop polling,
// the same rule as the other touch boards.
bool dhalTouch(int16_t &x, int16_t &y) {
    if (s_touchOff) return false;
    int32_t tx = 0, ty = 0;
    const uint32_t t0 = millis();
    const bool touched = s_lcd.getTouch(&tx, &ty);
    if (millis() - t0 >= kTouchSlowMs) {
        if (++s_touchFails >= 3) {
            s_touchOff = true;
            Serial.printf("[cs] display: touch read failed 3 times; touch off\n");
        }
        return false;
    }
    s_touchFails = 0;
    if (!touched) return false;
    x = (int16_t)tx;
    y = (int16_t)ty;
    return true;
}

void dhalBacklight(uint8_t level) { s_lcd.setBrightness(level); }

void dhalOff() {
    if (s_panelUp) s_lcd.setBrightness(0);   // don't leave a lit black panel
}

bool dhalWakePressed() {
    return s_wake.update(digitalRead(WAKE_BUTTON_PIN) == LOW, millis());
}
