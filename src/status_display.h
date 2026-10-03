#pragma once
// OLED status page (spec §5.6).
#include <stdint.h>

struct DisplayStatus {
    const char *nodeName;
    char        ip[24];
    const char *mqtt;
    int         storedMessages;   // across all monitored channels
    uint8_t     battState;        // BatteryState (battery_level.h)
    uint8_t     battPct;
};

#define CS_VERSION "0.1.0"

// Shows the splash (camillia-mt's camellia, drawn 1-bit) until displayUpdate
// is first allowed to draw, at least kSplashMs after this call.
void displayBegin();
void displayUpdate(const DisplayStatus &st);   // redraws at most once a second
