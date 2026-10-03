#pragma once
// OLED status page (spec §5.6).
#include <stdint.h>

struct DisplayStatus {
    const char *nodeName;
    char        ip[24];
    const char *mqtt;
    int         storedMessages;   // across all monitored channels
};

void displayBegin();
void displayUpdate(const DisplayStatus &st);   // redraws at most once a second
