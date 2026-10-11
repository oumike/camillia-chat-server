// LilyGo T-LoRa Pager (tlora-pager-tft). Early pin state follows camillia-mt's
// setup() and keyboard begin (camillia-mt 3777e74: src/main_lvgl.cpp,
// src/keyboard.cpp). The XL9555 rails are armed later by the vendored
// mesh_radio.cpp, as in camillia-mt.
#include "board_init.h"
#include <Arduino.h>
#include "board.h"

void boardEarlyInit() {
    // One SPI bus, three devices: keep radio and SD off it while the panel inits.
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    // No keyboard in this build; don't leave its backlight floating.
    pinMode(KB_BL, OUTPUT);
    digitalWrite(KB_BL, LOW);
    pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
}

void boardReport() {}
