#pragma once
// Status display, shared by the OLED (heltec-v4) and the expansion TFT backends.
// The OLED backend ignores the fields below the battery ones.
#include <stdint.h>
#include "activity_log.h"
#include "channel_store.h"
#include "node_names.h"
#include "settings.h"

struct DisplayStatus {
    const char *nodeName;
    char        ip[24];
    const char *mqtt;
    int         storedMessages;   // across all monitored channels
    uint8_t     battState;        // BatteryState (battery_level.h)
    uint8_t     battPct;

    // Expansion display (spec §7).
    const Settings *settings;
    uint32_t    nodeId;
    uint32_t    uptimeSec;
    bool        clockSet;
    float       battVolts;
    uint32_t    mqttRx, mqttDecrypted;
    bool        haveLastRx;
    uint32_t    lastRxAgeSec;
    float       lastRssi, lastSnr;
    float       airtimePct;
    uint8_t     dutyLimitPct;
    int         chanCount;
    int         chanCounts[CS_MAX_CHANNELS];
    uint32_t    flashUsedKB, flashTotalKB, psramFreeKB;
    // Newest-first copies for pages 2 and 3; only called when the page is shown.
    int (*newestMessages)(StoredMsg *out, int8_t *slots, int max);
    int (*newestActivity)(ActivityEntry *out, int max);
    const char *(*chanName)(int slot);
    const NodeNames *names;
};

// Set from the VERSION file by tools/set_app_version.py.
#ifndef CS_VERSION
#define CS_VERSION "dev"
#endif

// Shows the splash (camillia-mt's camellia, drawn 1-bit) until displayUpdate
// is first allowed to draw, at least kSplashMs after this call.
void displayBegin();
void displayUpdate(const DisplayStatus &st);   // redraws at most once a second
