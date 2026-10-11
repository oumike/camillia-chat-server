// Attaky Mesh Deck (mesh-deck): I2C bus recovery, then the FT6636 out of
// reset through AW9523 0x59 P13. Sequence, timings and order are camillia-mt's
// setup() for DEVICE_MESH_DECK — meshDeckI2cBusRecover(), Wire.begin(.., 100 kHz),
// meshDeckReleaseTouchReset() (camillia-mt 3777e74: src/main_lvgl.cpp) — all
// before the panel comes up. Every rail on the frame is always on; there is
// nothing else to power. The serial output those functions print is deferred
// to boardReport(), since this runs before Serial.begin().
#include "board_init.h"
#include <Arduino.h>
#include <Wire.h>
#include "board.h"
#include "mesh_deck_io.h"

namespace {

Aw9523 s_buttons;
bool   s_buttonsReady = false;
bool   s_sdaWasLow = false;
bool   s_sdaReleased = true;
bool   s_touchAck = false;

// A CPU reset can land mid-transfer and leave a slave holding SDA low; nine
// clocks and a STOP release it. Run unconditionally (camillia-mt does the same:
// a high SDA may just be between low bits of a byte in flight).
void i2cBusRecover() {
    pinMode(I2C_SDA, INPUT_PULLUP);
    pinMode(I2C_SCL, INPUT_PULLUP);
    delayMicroseconds(10);
    s_sdaWasLow = digitalRead(I2C_SDA) == LOW;

    pinMode(I2C_SCL, OUTPUT_OPEN_DRAIN);
    digitalWrite(I2C_SCL, HIGH);
    for (int i = 0; i < 9; i++) {
        digitalWrite(I2C_SCL, LOW);
        delayMicroseconds(5);
        digitalWrite(I2C_SCL, HIGH);
        delayMicroseconds(5);
    }

    // STOP: SDA released while SCL is high.
    pinMode(I2C_SDA, OUTPUT_OPEN_DRAIN);
    digitalWrite(I2C_SDA, LOW);
    delayMicroseconds(5);
    digitalWrite(I2C_SCL, HIGH);
    delayMicroseconds(5);
    digitalWrite(I2C_SDA, HIGH);
    delayMicroseconds(5);

    pinMode(I2C_SDA, INPUT_PULLUP);
    pinMode(I2C_SCL, INPUT_PULLUP);
    delayMicroseconds(10);
    s_sdaReleased = digitalRead(I2C_SDA) == HIGH;
}

// P13 low for 10 ms, then released; 300 ms is the FT6636's own boot time.
void releaseTouchReset() {
    if (!s_buttons.begin(BTN_EXPANDER_ADDR, Wire)) return;
    s_buttonsReady = true;
    constexpr uint8_t kP13 = 1u << TOUCH_RST_EXPANDER_BIT;
    uint8_t cfg = 0xFF, out = 0xFF;
    s_buttons.readReg(Aw9523::REG_CONFIG1, cfg);
    s_buttons.readReg(Aw9523::REG_OUTPUT1, out);
    s_buttons.writeReg(Aw9523::REG_OUTPUT1, (uint8_t)(out & ~kP13));   // assert reset
    s_buttons.writeReg(Aw9523::REG_CONFIG1, (uint8_t)(cfg & ~kP13));   // P13 -> output
    delay(10);
    s_buttons.writeReg(Aw9523::REG_OUTPUT1, (uint8_t)(out | kP13));    // release
    delay(300);

    Wire.beginTransmission((uint8_t)TOUCH_ADDR);
    s_touchAck = Wire.endTransmission() == 0;
}

}  // namespace

Aw9523 &meshDeckButtons() { return s_buttons; }
bool meshDeckButtonsReady() { return s_buttonsReady; }

void boardEarlyInit() {
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    i2cBusRecover();
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);
    releaseTouchReset();
}

void boardReport() {
    if (s_sdaWasLow || !s_sdaReleased) {
        Serial.printf("[cs] board: I2C recovery: sda was %s, now %s\n", s_sdaWasLow ? "LOW" : "high",
                      s_sdaReleased ? "released" : "STILL LOW");
    }
    if (!s_buttonsReady) {
        Serial.printf("[cs] board: touch reset expander 0x%02x not found\n", BTN_EXPANDER_ADDR);
        return;
    }
    Serial.printf("[cs] board: FT6636 out of reset, 0x%02x %s\n", TOUCH_ADDR, s_touchAck ? "ACK" : "NO ACK");
    if (!s_touchAck) {
        Serial.print("[cs] board: I2C scan:");
        for (uint8_t a = 0x08; a < 0x78; a++) {
            Wire.beginTransmission(a);
            if (Wire.endTransmission() == 0) Serial.printf(" 0x%02x", a);
        }
        Serial.println();
    }
}
