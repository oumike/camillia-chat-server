// Display hardware for the Attaky Mesh Deck (mesh-deck): ST7789 through
// LovyanGFX at rotation 3, PWM backlight, FT6636 touch read over Wire, and a
// Wake button (BTN_R2 on expander 0x59, or the IO38 Boot button).
// Touch read and transform: camillia-mt 3777e74 src/main_lvgl.cpp
// meshDeckReadTouch(); button polling: meshDeckPollButtons() / pollUserButton().
#include "display_hal.h"
#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>
#include "board.h"
#include "mesh_deck_display.h"
#include "mesh_deck_io.h"
#include "wake_edge.h"

namespace {

constexpr uint32_t kTouchSlowMs = 200;   // a healthy FT6636 read takes ~1 ms; a bus timeout far longer
constexpr uint32_t kButtonPollMs = 40;   // camillia-mt's expander poll spacing ("debounce + bus courtesy")

LGFX_MeshDeck s_lcd;
bool          s_panelUp = false;   // lcd.init() succeeded; dhalOff() may then blank the backlight
bool          s_touchOff = false;
uint8_t       s_touchFails = 0;
WakeEdge      s_wake;
uint32_t      s_btnNextMs = 0;
bool          s_r2Down = false;

// FT6x36 standard layout: one 7-byte burst from TD_STATUS (0x02) holds the
// touch count and the first point's 12-bit X/Y, in native portrait 240x320.
// Returns -1 on a bus error, 0 for no touch, 1 for a touch.
int readTouch(int16_t &outX, int16_t &outY) {
    Wire.beginTransmission((uint8_t)TOUCH_ADDR);
    Wire.write((uint8_t)0x02);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom((int)TOUCH_ADDR, 7) != 7) return -1;
    uint8_t b[7];
    for (int i = 0; i < 7; i++) b[i] = (uint8_t)Wire.read();

    const uint8_t points = b[0] & 0x0F;
    if (points == 0 || points > 2) return 0;   // 0xFF etc. = nothing valid

    int32_t x = ((b[1] & 0x0F) << 8) | b[2];
    int32_t y = ((b[3] & 0x0F) << 8) | b[4];
    if (x >= TFT_PANEL_WIDTH) x = TFT_PANEL_WIDTH - 1;
    if (y >= TFT_PANEL_HEIGHT) y = TFT_PANEL_HEIGHT - 1;
    // Rotation 3: screen x runs along native y reversed, screen y along native x.
    outX = (int16_t)((TFT_PANEL_HEIGHT - 1) - y);
    outY = (int16_t)x;
    return 1;
}

}  // namespace

bool dhalBegin(char *err, size_t cap) {
    // boardEarlyInit() already took the FT6636 out of reset, before lcd.init()
    // as camillia-mt orders it.
    if (!s_lcd.init()) {
        snprintf(err, cap, "panel init failed");
        return false;
    }
    s_panelUp = true;
    s_lcd.setRotation(TFT_ROTATION_LANDSCAPE);
    s_lcd.setBrightness(TFT_BRIGHTNESS_DEFAULT);
    s_lcd.fillScreen(TFT_BLACK);

    bool touchAck = false;
    for (int attempt = 0; attempt < 3 && !touchAck; attempt++) {
        if (attempt) delay(100);
        Wire.beginTransmission((uint8_t)TOUCH_ADDR);
        touchAck = Wire.endTransmission() == 0;
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

// Three slow bus errors in a row: stop polling, so a dead controller cannot
// stall the loop on every frame.
bool dhalTouch(int16_t &x, int16_t &y) {
    if (s_touchOff) return false;
    const uint32_t t0 = millis();
    const int r = readTouch(x, y);
    if (r < 0 && millis() - t0 >= kTouchSlowMs) {
        if (++s_touchFails >= 3) {
            s_touchOff = true;
            Serial.printf("[cs] display: touch read failed 3 times; touch off\n");
        }
        return false;
    }
    if (r >= 0) s_touchFails = 0;
    return r > 0;
}

void dhalBacklight(uint8_t level) { s_lcd.setBrightness(level); }

void dhalOff() {
    if (s_panelUp) s_lcd.setBrightness(0);   // don't leave a lit black panel
}

// camillia-mt binds both of these to screen sleep/wake: BTN_R2 (expander 0x59
// P06, active-low) and the IO38 Boot button (active-low GPIO).
bool dhalWakePressed() {
    const uint32_t now = millis();
    if (meshDeckButtonsReady() && (int32_t)(now - s_btnNextMs) >= 0) {
        s_btnNextMs = now + kButtonPollMs;
        uint8_t p0 = 0xFF;
        // All eight reading pressed at once is a bad read, not a hand (camillia-mt).
        if (meshDeckButtons().readPort(0, p0) && p0 != 0x00) {
            s_r2Down = (p0 & (1u << BTN_R2_BIT)) == 0;
        } else {
            s_r2Down = false;
        }
    }
    const bool bootDown = digitalRead(BOOT_BUTTON_PIN) == LOW;
    return s_wake.update(s_r2Down || bootDown, now);
}
