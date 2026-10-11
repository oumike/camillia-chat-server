// The CrowPanel Advance boards have no battery sense: the charger has no ADC
// tap on either schematic (camillia-mt 3777e74: src/hal/hw_crowpanel_35.h and
// hw_crowpanel_50.h, BATT_ADC_PIN -1). batteryVolts() stays 0, which
// battery_level.cpp reports as BATT_ABSENT (no battery figure, mains power).
#include "battery.h"

void batteryBegin() {}
void batteryLoop(uint32_t) {}
float batteryVolts() { return 0.0f; }
