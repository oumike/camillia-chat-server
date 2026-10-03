#pragma once
// Battery voltage → percent / state for the OLED indicator. Pure C++.
#include <stdint.h>

enum BatteryState : uint8_t {
    BATT_ABSENT,     // nothing on the battery connector
    BATT_EXTERNAL,   // above a full cell: USB power / charging
    BATT_PRESENT,
};

// Same Li-ion open-circuit curve as camillia-mt's battery_util.cpp, so both
// devices show the same number for the same cell.
uint8_t batteryPercentFromVolts(float v);
BatteryState batteryStateFromVolts(float v);
