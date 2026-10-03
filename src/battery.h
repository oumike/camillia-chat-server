#pragma once
// Heltec V4 battery voltage: switched divider on BATT_ADC_PIN, sampled every 10 s.
#include <stdint.h>

void batteryBegin();
void batteryLoop(uint32_t nowMs);
float batteryVolts();   // smoothed; 0 until the first sample
