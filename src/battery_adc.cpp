#include "battery.h"
#include <Arduino.h>
#include "board.h"

static float    s_volts = 0;
static uint32_t s_nextMs = 0;
static constexpr uint32_t kPeriodMs = 10000;

// V4 board revisions differ in the sense-enable polarity (camillia-mt defaults
// LOW, Meshtastic's heltec_v4 uses HIGH), so like camillia-mt's battery_util.cpp
// we try both on the first read and keep whichever actually reads the divider.
static int  s_senseOn = BATT_SENSE_ON;
static bool s_senseLocked = false;
static constexpr float kPickMarginV = 0.03f;   // at the ADC pin
static constexpr float kDeadAdcV = 0.05f;

static void setSense(int level) {
#if BATT_SENSE_PIN >= 0
    digitalWrite(BATT_SENSE_PIN, level);
#else
    (void)level;
#endif
}

static float sampleAdcVolts(int senseLevel) {
    setSense(senseLevel);
    delayMicroseconds(500);   // let the divider settle
    uint32_t mv = 0;
    for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(BATT_ADC_PIN);
    return (mv / 8.0f) / 1000.0f;
}

void batteryBegin() {
#if BATT_SENSE_PIN >= 0
    pinMode(BATT_SENSE_PIN, OUTPUT);
    setSense(!s_senseOn);
#else
    s_senseLocked = true;   // divider permanently connected (V4-R8)
#endif
    analogSetPinAttenuation(BATT_ADC_PIN, ADC_11db);
    s_nextMs = 0;
}

void batteryLoop(uint32_t nowMs) {
    if (s_nextMs && (int32_t)(nowMs - s_nextMs) < 0) return;
    s_nextMs = nowMs + kPeriodMs;
    if (!s_senseLocked) {
        float vDef = sampleAdcVolts(s_senseOn);
        float vOpp = sampleAdcVolts(!s_senseOn);
        if (vOpp > vDef + kPickMarginV) s_senseOn = !s_senseOn;
        s_senseLocked = true;
        Serial.printf("[batt] sense GPIO%d active %s (ADC %.2f V at default, %.2f V opposite)\n",
                      BATT_SENSE_PIN, s_senseOn ? "HIGH" : "LOW", vDef, vOpp);
    }
    float adc = sampleAdcVolts(s_senseOn);
    if (BATT_SENSE_PIN >= 0 && adc < kDeadAdcV) {
        // Battery may have been plugged in after the first probe.
        float alt = sampleAdcVolts(!s_senseOn);
        if (alt > adc + kPickMarginV) {
            s_senseOn = !s_senseOn;
            adc = alt;
        }
    }
    setSense(!s_senseOn);
    float v = adc * BATT_DIV;
    s_volts = (s_volts <= 0.1f || v < 2.5f) ? v : s_volts * 0.7f + v * 0.3f;
}

float batteryVolts() { return s_volts; }
