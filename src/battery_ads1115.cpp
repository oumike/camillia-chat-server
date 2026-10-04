// Wio Tracker L2 battery voltage: ADS1115 AIN0 through a /2 divider that
// expander bit 15 switches on only around a read; sampled every 10 s. ADS1115
// handling follows camillia-mt src/battery_util.cpp, except the gain: GAIN_ONE
// (+/-4.096 V) as in upstream Meshtastic, since GAIN_TWO clips a full cell at 4.096 V.
#include "battery.h"
#include <Arduino.h>
#include <Adafruit_ADS1X15.h>
#include <Wire.h>
#include "board.h"

namespace {

constexpr uint32_t kPeriodMs = 10000;
constexpr uint32_t kConvTimeoutMs = 20;   // one conversion at the default 128 SPS takes ~8 ms

Adafruit_ADS1115 s_ads;
bool     s_found = false;
float    s_volts = 0;
uint32_t s_nextMs = 0;

// readADC_SingleEnded() with a timeout: the library's version waits for the
// conversion forever, so a bus that stopped answering would hang the loop.
bool readVolts(float &v) {
    s_ads.startADCReading(MUX_BY_CHANNEL[ADS1115_BATT_CHANNEL], /*continuous=*/false);
    const uint32_t t0 = millis();
    while (!s_ads.conversionComplete()) {
        if (millis() - t0 >= kConvTimeoutMs) return false;
        delay(1);
    }
    v = s_ads.computeVolts(s_ads.getLastConversionResults()) * BATT_DIV;
    return true;
}

}  // namespace

void batteryBegin() {
    s_nextMs = 0;
    // Sense on for the probe as well, as camillia-mt and Meshtastic both have it.
    boardExpanderSet(EXP_BIT_BATT_SENSE_EN, true);
    delay(10);
    s_found = s_ads.begin(ADS1115_ADDR, &Wire);
    boardExpanderSet(EXP_BIT_BATT_SENSE_EN, false);
    if (!s_found) {
        Serial.printf("[cs] battery: ADS1115 not found at 0x%02x\n", ADS1115_ADDR);
        return;
    }
    s_ads.setGain(GAIN_ONE);
    Serial.printf("[cs] battery: ADS1115 ok at 0x%02x\n", ADS1115_ADDR);
}

void batteryLoop(uint32_t nowMs) {
    if (!s_found) return;   // batteryVolts() stays 0: shown as absent
    if (s_nextMs && (int32_t)(nowMs - s_nextMs) < 0) return;
    s_nextMs = nowMs + kPeriodMs;
    boardExpanderSet(EXP_BIT_BATT_SENSE_EN, true);
    delay(2);   // let the divider settle
    float v = 0;
    const bool ok = readVolts(v);
    boardExpanderSet(EXP_BIT_BATT_SENSE_EN, false);
    if (!ok) return;
    s_volts = (s_volts <= 0.1f || v < 2.5f) ? v : s_volts * 0.7f + v * 0.3f;
}

float batteryVolts() { return s_volts; }
