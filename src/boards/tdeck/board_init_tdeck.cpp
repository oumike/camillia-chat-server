// LilyGo T-Deck (tdeck). Power-up order follows camillia-mt's setup()
// (camillia-mt 3777e74: src/main_lvgl.cpp, BOARD_POWERON before anything else).
#include "board_init.h"
#include <Arduino.h>
#include "board.h"

void boardEarlyInit() {
    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
    // One SPI bus, three devices: keep radio and SD off it while the panel inits.
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
}

void boardReport() {}
