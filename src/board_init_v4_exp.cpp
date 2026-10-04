// Heltec V4 on the TFT expansion kit (heltec-v4-expansion).
#include "board_init.h"
#include <Arduino.h>
#include "board.h"

void boardEarlyInit() {
    // VEXT (GPIO36) LOW first, and never again: HIGH breaks touch I2C, and
    // raising it late stops this board booting. See board_v4_exp.h.
    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, VEXT_ON_LEVEL);
    // GPIO7 (BOARD_POWERON in camillia-mt main_lvgl.cpp setup()) HIGH before lcd.init(),
    // not only later in radioBegin().
    pinMode(LORA_FEM_POWER_PIN, OUTPUT);
    digitalWrite(LORA_FEM_POWER_PIN, HIGH);
}

void boardReport() {}
