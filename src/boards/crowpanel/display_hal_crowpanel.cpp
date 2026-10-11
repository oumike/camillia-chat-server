// Display hardware for the Elecrow CrowPanel Advance 3.5" (crowpanel-35:
// ILI9488 on SPI, PWM backlight) and 5.0" (crowpanel-50: 800x480 RGB panel
// from a PSRAM framebuffer, backlight on a PCA9557 or the board MCU), both
// with GT911 touch. Neither board has a free button, so there is no Wake
// button. Init order follows camillia-mt's setup() (camillia-mt 3777e74:
// src/main_lvgl.cpp, lcd.init() then setRotation, setBrightness, fillScreen).
#include "display_hal.h"
#include <Arduino.h>
#include <stdio.h>
#include "board.h"

#if defined(DEVICE_CROWPANEL_35)
#include "crowpanel_35_display.h"
using CrowPanelLcd = LGFX_CrowPanel35;
#else
#include "crowpanel_50_display.h"
using CrowPanelLcd = LGFX_CrowPanel50;
#endif

namespace {

constexpr uint32_t kTouchSlowMs = 200;   // a healthy GT911 read takes ~1 ms; a bus timeout far longer

CrowPanelLcd s_lcd;
bool         s_panelUp = false;   // lcd.init() succeeded; dhalOff() may then blank the backlight
bool         s_touchOff = false;
uint8_t      s_touchFails = 0;

}  // namespace

bool dhalBegin(char *err, size_t cap) {
    // The 5.0" RGB panel allocates its 750 KB framebuffer in PSRAM here.
    if (!s_lcd.init()) {
        snprintf(err, cap, "panel init failed");
        return false;
    }
    s_panelUp = true;
    s_lcd.setRotation(TFT_ROTATION_LANDSCAPE);
    s_lcd.setBrightness(TFT_BRIGHTNESS_DEFAULT);
    s_lcd.fillScreen(TFT_BLACK);

    // LovyanGFX drops the touch device when the GT911 never answered its probe.
    if (!s_lcd.touch()) {
        s_touchOff = true;
        Serial.printf("[cs] display: touch controller not answering; touch off\n");
    }
    return true;
}

// On the 5.0" this is a copy into the framebuffer (Panel_RGB writes back the
// cache lines it touched); on the 3.5" an SPI transfer.
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

bool dhalWakePressed() { return false; }   // no free button: BOOT/GPIO0 is LoRa NSS (3.5", 5.0" V1.0)
