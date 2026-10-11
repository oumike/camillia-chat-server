// Elecrow CrowPanel Advance 3.5" / 5.0" (crowpanel-35, crowpanel-50). There is
// no power rail to raise: the board is powered whenever it is. All that is
// done here is keeping the radio deselected until mesh_radio.cpp starts it,
// and on the 3.5" routing the expansion slot to the radio (IO45 LOW,
// camillia-mt 3777e74: src/hal/hw_crowpanel_35.h LORA_POWER_ENABLE_PIN) and
// deselecting the unused microSD card.
#include "board_init.h"
#include <Arduino.h>
#include "board.h"

void boardEarlyInit() {
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
#if LORA_POWER_ENABLE_PIN >= 0
    pinMode(LORA_POWER_ENABLE_PIN, OUTPUT);
    digitalWrite(LORA_POWER_ENABLE_PIN, LORA_POWER_ENABLE_LEVEL);
#endif
#if defined(DEVICE_CROWPANEL_35)
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
#endif
}

// The 5.0"'s backlight controller (PCA9557 / board MCU) is probed, and
// reported, when the display starts.
void boardReport() {}
