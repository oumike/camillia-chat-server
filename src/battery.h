#pragma once
// Battery voltage, sampled every 10 s. One implementation per board, chosen by
// build_src_filter in platformio.ini: battery_adc.cpp (Heltec V4, switched
// divider on BATT_ADC_PIN), battery_ads1115.cpp (Wio Tracker L2, ADS1115).
#include <stdint.h>

void batteryBegin();
void batteryLoop(uint32_t nowMs);
float batteryVolts();   // smoothed; 0 until the first sample
