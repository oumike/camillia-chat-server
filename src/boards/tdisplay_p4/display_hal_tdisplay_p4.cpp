// Display hardware for the LilyGo T-Display P4: RM69A10 MIPI-DSI AMOLED, DCS
// brightness and GT9895 touch through camillia-mt's LovyanGFX driver
// (hal/tdisplay_p4_display.h). LVGL draws TFT_LANDSCAPE_W x TFT_LANDSCAPE_H;
// each of its pixels goes out as a TDISPLAY_P4_UI_SCALE square block, offset
// TDISPLAY_P4_INSET_X from the panel's short edges (board_tdisplay_p4.h), as
// camillia-mt's pushPixelScaled() does. Wake button: GPIO35 (BOOT), active LOW.
#include "display_hal.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "hal/tdisplay_p4_display.h"
#include "wake_edge.h"

namespace {

constexpr int32_t kScale = TDISPLAY_P4_UI_SCALE;
constexpr int32_t kOffX = TDISPLAY_P4_INSET_X;
constexpr uint32_t kTouchSlowMs = 200;   // a GT9895 read is a few ms; a bus timeout far longer

LGFX_TDeck s_lcd;
bool       s_panelUp = false;
bool       s_touchOff = false;
uint8_t    s_touchFails = 0;
WakeEdge   s_wake;
// One expanded source row, UI_SCALE rows tall.
lgfx::rgb565_t s_rows[TFT_LANDSCAPE_W * kScale * kScale];

}  // namespace

bool dhalBegin(char *err, size_t cap) {
    // boardEarlyInit() brought the XL9535 up; init_impl() pulses the panel
    // reset through it, and init() also brings the GT9895 up.
    if (!s_lcd.init()) {
        snprintf(err, cap, "panel init failed");
        return false;
    }
    s_panelUp = true;
    s_lcd.setRotation(TFT_ROTATION_LANDSCAPE);
    s_lcd.setBrightness(TFT_BRIGHTNESS_DEFAULT);
    s_lcd.fillScreen(TFT_BLACK);
    if (!s_lcd.touch()) {
        s_touchOff = true;
        Serial.printf("[cs] display: GT9895 touch not found; touch off\n");
    }
    return true;
}

void dhalFlush(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *px) {
    const lgfx::rgb565_t *src = (const lgfx::rgb565_t *)px;
    if (kScale == 1) {
        s_lcd.pushImage(x + kOffX, y, w, h, src);
        return;
    }
    if (w <= 0 || h <= 0 || w > TFT_LANDSCAPE_W) return;
    const int32_t sw = w * kScale;
    s_lcd.startWrite();
    for (int32_t row = 0; row < h; row++) {
        const lgfx::rgb565_t *in = src + (size_t)row * (size_t)w;
        lgfx::rgb565_t *out = s_rows;
        for (int32_t i = 0; i < w; i++) {
            for (int32_t k = 0; k < kScale; k++) *out++ = in[i];
        }
        for (int32_t r = 1; r < kScale; r++) {
            memcpy(s_rows + (size_t)r * (size_t)sw, s_rows, (size_t)sw * sizeof(s_rows[0]));
        }
        s_lcd.pushImage(x * kScale + kOffX, (y + row) * kScale, sw, kScale, s_rows);
    }
    s_lcd.endWrite();
}

// A failed GT9895 read returns no points, the same as no touch, so a failure
// is one that took a bus timeout. Three in a row: stop polling.
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
    tx = (tx - kOffX) / kScale;
    ty = ty / kScale;
    if (tx < 0) tx = 0;
    if (tx >= TFT_LANDSCAPE_W) tx = TFT_LANDSCAPE_W - 1;
    if (ty >= TFT_LANDSCAPE_H) ty = TFT_LANDSCAPE_H - 1;
    x = (int16_t)tx;
    y = (int16_t)ty;
    return true;
}

void dhalBacklight(uint8_t level) { s_lcd.setBrightness(level); }

void dhalOff() {
    if (s_panelUp) s_lcd.setBrightness(0);
}

bool dhalWakePressed() {
    return s_wake.update(digitalRead(USER_BUTTON_PIN) == USER_BUTTON_ACTIVE_LEVEL, millis());
}
