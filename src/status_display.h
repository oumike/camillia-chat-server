#pragma once
// OLED status page (spec §5.6).
#include <stdint.h>

struct DisplayStatus {
    const char *nodeName;
    char        ip[24];
    const char *mqtt;
    bool        everSent;
    uint32_t    lastSentUnix;     // 0 if the clock was not set
    uint32_t    lastSentUptimeSec;
};

void displayBegin();
void displayUpdate(const DisplayStatus &st);   // redraws at most once a second
