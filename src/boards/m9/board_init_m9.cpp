// Elecrow ThinkNode M9 (m9). Power-up follows camillia-mt's setup()
// (camillia-mt 3777e74: src/main_lvgl.cpp, BOARD_VEXT_ENABLE driven to its on
// level, then a 20 ms settle, before the panel or radio is touched).
#include "board_init.h"
#include <Arduino.h>
#include "board.h"

void boardEarlyInit() {
    pinMode(BOARD_VEXT_ENABLE, OUTPUT);
    digitalWrite(BOARD_VEXT_ENABLE, BOARD_VEXT_ON_LEVEL);
    // GPS shares the rail but is not used: hold it disabled, reset released.
    pinMode(GPS_ENABLE_PIN, OUTPUT);
    digitalWrite(GPS_ENABLE_PIN, !GPS_ENABLE_ON_LEVEL);
    pinMode(GPS_RESET_PIN, OUTPUT);
    digitalWrite(GPS_RESET_PIN, !GPS_RESET_ACTIVE_LEVEL);
    pinMode(BOARD_BUZZER, OUTPUT);
    digitalWrite(BOARD_BUZZER, LOW);
    // One SPI bus, three devices: keep radio and SD off it while the panel inits.
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
    delay(20);
}

void boardReport() {}
