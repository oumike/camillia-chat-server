// LilyGo T-Deck Pro (tdeck-pro). camillia-mt 3777e74 has no board power rail
// (BOARD_POWERON -1 in src/hal/hw_tdeck_pro.h); the LoRa module's own rail
// (GPIO46) is raised by mesh_radio.cpp. Here: park the shared-bus chip selects
// (as LGFX_TDeck::init() in src/hal/tdeck_pro_display.h does) and switch off
// the lights and GNSS this firmware never uses.
#include "board_init.h"
#include <Arduino.h>
#include "board.h"

void boardEarlyInit() {
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    pinMode(EINK_CS, OUTPUT);
    digitalWrite(EINK_CS, HIGH);
    pinMode(FRONTLIGHT_PIN, OUTPUT);
    digitalWrite(FRONTLIGHT_PIN, LOW);
    pinMode(KB_BL, OUTPUT);
    digitalWrite(KB_BL, LOW);
    pinMode(GPS_ENABLE_PIN, OUTPUT);
    digitalWrite(GPS_ENABLE_PIN, !GPS_ENABLE_ON_LEVEL);
}

void boardReport() {}
