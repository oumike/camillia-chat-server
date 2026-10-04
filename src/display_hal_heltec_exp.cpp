// Display hardware for the Heltec V4 expansion kit (heltec-v4-expansion):
// ST7789 through LovyanGFX, PWM backlight, CHSC6X touch on Wire1.
#include "display_hal.h"
#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>
#include "hal_v4_exp_display.h"
#include "board.h"

namespace {

constexpr uint32_t kTouchSlowMs = 200;   // a healthy CHSC6X read takes ~1 ms; a bus timeout ~1 s

LGFX_V4Exp s_lcd;
bool       s_touchOff = false;
bool       s_panelUp = false;   // lcd.init() succeeded; dhalOff() may then blank the backlight
uint8_t    s_touchFails = 0;

}  // namespace

bool dhalBegin(char *err, size_t cap) {
    // GPIO36 (VEXT) is already LOW from the top of setup(); not touched here.
    if (!s_lcd.init()) {
        snprintf(err, cap, "panel init failed");
        return false;
    }
    s_panelUp = true;
    s_lcd.setRotation(TFT_ROTATION_LANDSCAPE);
    s_lcd.setBrightness(TFT_BRIGHTNESS_DEFAULT);
    s_lcd.fillScreen(TFT_BLACK);

    // lcd.init() started the CHSC6X on Wire1; if it does not ACK, never poll it.
    // chsc6x_init only waits 30 ms after reset, so retry the probe before giving up.
    bool touchAck = false;
    for (int attempt = 0; attempt < 3 && !touchAck; attempt++) {
        delay(100);
        Wire1.beginTransmission((uint8_t)TOUCH_ADDR);
        touchAck = (Wire1.endTransmission() == 0);
    }
    if (!touchAck) {
        s_touchOff = true;
        Serial.printf("[cs] display: touch controller not answering; touch off\n");
    }
    return true;
}

void dhalFlush(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *px) {
    s_lcd.pushImage(x, y, w, h, (const lgfx::rgb565_t *)px);
}

// The CHSC6X library reports "no touch" and "bus error" the same way, so a
// failed read is one that took a bus timeout. Three in a row: stop polling.
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

bool dhalWakePressed() { return false; }   // no Wake button on this board
