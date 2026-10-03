#include "battery.h"
#include <Arduino.h>
#include "board.h"

static float    s_volts = 0;
static uint32_t s_nextMs = 0;
static constexpr uint32_t kPeriodMs = 10000;

void batteryBegin() {
    pinMode(BATT_SENSE_PIN, OUTPUT);
    digitalWrite(BATT_SENSE_PIN, !BATT_SENSE_ON);
    analogSetPinAttenuation(BATT_ADC_PIN, ADC_11db);
    s_nextMs = 0;
}

void batteryLoop(uint32_t nowMs) {
    if (s_nextMs && (int32_t)(nowMs - s_nextMs) < 0) return;
    s_nextMs = nowMs + kPeriodMs;
    digitalWrite(BATT_SENSE_PIN, BATT_SENSE_ON);
    delayMicroseconds(500);   // let the divider settle
    uint32_t mv = 0;
    for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(BATT_ADC_PIN);
    digitalWrite(BATT_SENSE_PIN, !BATT_SENSE_ON);
    float v = (mv / 8.0f) / 1000.0f * BATT_DIV;
    s_volts = (s_volts <= 0.1f || v < 2.5f) ? v : s_volts * 0.7f + v * 0.3f;
}

float batteryVolts() { return s_volts; }
