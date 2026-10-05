// Display hardware for the Seeed Wio Tracker L2 (wio-tracker-l2): NV3031B on
// quad-SPI through LovyanGFX, LP5814 I2C backlight, GT911 touch, and the Wake
// button on expander bit 0. Every one of them sits behind the expander's
// rails, so nothing is touched unless board_init_wio_l2.cpp brought it up.
#include "display_hal.h"
#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>
#include "hal_wio_l2_display.h"
#include "board.h"
#include "wake_edge.h"

namespace {

constexpr uint32_t kTouchSlowMs = 200;    // a healthy GT911 read takes ~1 ms; a bus timeout far longer
constexpr uint32_t kWakePollMs  = 15;     // camillia-mt's expander read spacing while the button is busy
constexpr uint8_t  kGt911AltAddr = 0x14;  // the GT911's other strap address; LovyanGFX tries both

LGFX_WioL2 s_lcd;
bool       s_panelUp = false;   // lcd.init() succeeded; dhalOff() may then blank the backlight
bool       s_touchOff = false;
uint8_t    s_touchFails = 0;
WakeEdge   s_wake;
uint32_t   s_wakeNextMs = 0;

bool i2cAck(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

}  // namespace

bool dhalBegin(char *err, size_t cap) {
    if (!boardExpanderOk()) {
        snprintf(err, cap, "expander not found");
        return false;
    }
    pinMode(EXPANDER_INT, INPUT_PULLUP);

    // init_impl (hal_wio_l2_display.h) brings up the LP5814 before the GT911
    // and resets Wire afterwards, in camillia-mt's order.
    if (!s_lcd.init()) {
        snprintf(err, cap, "panel init failed");
        return false;
    }
    s_panelUp = true;
    s_lcd.setRotation(TFT_ROTATION_LANDSCAPE);
    s_lcd.setBrightness(TFT_BRIGHTNESS_DEFAULT);
    s_lcd.fillScreen(TFT_BLACK);

    // Board init released the GT911 reset 60 ms before setup() went on; retry
    // the probe before giving up on it.
    bool touchAck = false;
    for (int attempt = 0; attempt < 3 && !touchAck; attempt++) {
        if (attempt) delay(100);
        touchAck = i2cAck(TOUCH_ADDR) || i2cAck(kGt911AltAddr);
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

// A failed read is one that took a bus timeout. Three in a row: stop polling,
// the same rule as the Heltec expansion.
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

// The light skips its register writes itself when the LP5814 never answered.
void dhalBacklight(uint8_t level) { s_lcd.setBrightness(level); }

void dhalOff() {
    if (s_panelUp) s_lcd.setBrightness(0);   // don't leave a lit black panel
}

// The expander pulls GPIO45 low on any input change until its input port is
// read, so the bus is only touched while that line is low or a press is in
// progress (to see it settle and release).
bool dhalWakePressed() {
    if (!boardExpanderOk()) return false;
    const uint32_t now = millis();
    if (digitalRead(EXPANDER_INT) != LOW && !s_wake.busy()) return false;
    if ((int32_t)(now - s_wakeNextMs) < 0) return false;
    s_wakeNextMs = now + kWakePollMs;

    uint16_t in = 0xFFFF;
    if (!boardExpanderRead(in)) {   // bus silent: let the press lapse rather than poll forever
        s_wake.update(false, now);
        return false;
    }
    const bool pressed = (in & (1u << EXP_BIT_WAKE_BUTTON)) == 0;   // active-low
    return s_wake.update(pressed, now);
}
