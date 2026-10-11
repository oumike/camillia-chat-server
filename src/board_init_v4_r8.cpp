// Heltec V4-R8 on the Expansion Kit V2 (heltec-v4-r8).
#include "board_init.h"
#include <Arduino.h>
#include "board.h"

void boardEarlyInit() {
    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, VEXT_ON_LEVEL);
    pinMode(LORA_FEM_POWER_PIN, OUTPUT);
    digitalWrite(LORA_FEM_POWER_PIN, HIGH);
    // Deselect the micro-SD before the display starts clocking the shared bus.
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
}

void boardReport() {}
