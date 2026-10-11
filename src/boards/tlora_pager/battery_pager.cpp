// T-LoRa Pager battery voltage: the BQ25896 charger's ADC (REG0E BATV) over
// I2C, sampled every 10 s. Register handling follows camillia-mt's
// src/battery_util.cpp batteryReadPagerBqVolts() (camillia-mt 3777e74): a
// one-shot conversion is started and collected on a later pass rather than
// blocking, and the charger's 40 s I2C watchdog is petted on every pass so it
// never restores register defaults.
#include "battery.h"
#include <Arduino.h>
#include <Wire.h>
#include "board.h"

namespace {

constexpr uint32_t kPeriodMs        = 10000;
constexpr uint32_t kCollectDelayMs  = 1500;    // one-shot conversion takes ~1 s at most
constexpr uint32_t kRetryBackoffMs  = 15000;
constexpr uint8_t  kRegAdcControl   = 0x02;    // bit 7 CONV_START, bit 6 CONV_RATE
constexpr uint8_t  kRegWatchdogRst  = 0x03;    // bit 6 WD_RST
constexpr uint8_t  kRegBattVoltage  = 0x0E;
constexpr uint16_t kVbatBaseMv      = 2304;
constexpr uint16_t kVbatStepMv      = 20;

bool     s_present = false;
bool     s_pending = false;   // one-shot started, result not yet read
float    s_volts = 0;
uint32_t s_nextMs = 0;

bool readReg(uint8_t reg, uint8_t &val) {
    Wire.beginTransmission(BQ25896_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)BQ25896_ADDR, 1) != 1) return false;
    val = Wire.read();
    return true;
}

bool writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(BQ25896_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

bool probe() {
    Wire.beginTransmission(BQ25896_ADDR);
    return Wire.endTransmission() == 0;
}

bool kickWatchdog() {
    uint8_t reg = 0;
    if (!readReg(kRegWatchdogRst, reg)) return false;
    return writeReg(kRegWatchdogRst, (uint8_t)(reg | 0x40));
}

bool startConversion() {
    uint8_t adc = 0;
    if (!readReg(kRegAdcControl, adc)) return false;
    adc &= (uint8_t)~0x40;   // one shot
    adc |= 0x80;             // CONV_START
    return writeReg(kRegAdcControl, adc);
}

void markUnavailable(uint32_t nowMs) {
    s_present = false;
    s_pending = false;
    s_nextMs = nowMs + kRetryBackoffMs;
}

// Returns false on a bus error. ready=false while the conversion runs; v stays
// 0 when the result is below the ADC floor.
bool collect(bool &ready, float &v) {
    v = 0;
    ready = false;
    uint8_t adc = 0;
    if (!readReg(kRegAdcControl, adc)) return false;
    if (adc & 0x80) return true;   // still converting
    ready = true;
    uint8_t reg = 0;
    if (!readReg(kRegBattVoltage, reg)) return false;
    const uint8_t raw = reg & 0x7F;
    if (raw) v = (kVbatBaseMv + raw * kVbatStepMv) / 1000.0f;
    return true;
}

}  // namespace

void batteryBegin() {
    Wire.begin(KB_SDA, KB_SCL);   // the same bus mesh_radio.cpp brings up for the XL9555
    s_nextMs = 0;
    s_present = probe() && startConversion();
    s_pending = s_present;
    if (!s_present) {
        Serial.printf("[cs] battery: BQ25896 not found at 0x%02x\n", BQ25896_ADDR);
        markUnavailable(millis());
        return;
    }
    s_nextMs = millis() + kCollectDelayMs;
    Serial.printf("[cs] battery: BQ25896 ok at 0x%02x\n", BQ25896_ADDR);
}

void batteryLoop(uint32_t nowMs) {
    if (s_nextMs && (int32_t)(nowMs - s_nextMs) < 0) return;
    if (!s_present) {
        if (!probe()) { markUnavailable(nowMs); return; }   // batteryVolts() holds; 0 = absent
        s_present = true;
        s_pending = false;
    }
    if (!kickWatchdog()) { markUnavailable(nowMs); return; }

    if (!s_pending) {
        s_pending = startConversion();
        if (!s_pending) { markUnavailable(nowMs); return; }
        s_nextMs = nowMs + kCollectDelayMs;
        return;
    }

    bool ready = false;
    float v = 0;
    if (!collect(ready, v)) { markUnavailable(nowMs); return; }
    if (!ready) { s_nextMs = nowMs + 500; return; }
    s_pending = false;
    s_nextMs = nowMs + kPeriodMs - kCollectDelayMs;
    if (v <= 0) return;   // BATV=0: indistinguishable from below-floor; hold the last reading
    s_volts = (s_volts <= 0.1f || v < 2.5f) ? v : s_volts * 0.7f + v * 0.3f;
}

float batteryVolts() { return s_volts; }
