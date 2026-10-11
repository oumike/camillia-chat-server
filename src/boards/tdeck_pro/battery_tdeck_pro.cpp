// T-Deck Pro battery: BATV from the BQ25896 charger's ADC over I2C. Vendored
// from camillia-mt 3777e74 src/battery_util.cpp (batteryReadPagerBqVolts and
// helpers): one-shot conversions, started on one sample and collected on the
// next, with the charger's 40 s I2C watchdog petted on every sample so it does
// not reset the ADC configuration.
#include "battery.h"
#include <Arduino.h>
#include <Wire.h>
#include "board.h"

namespace {
constexpr uint8_t  kAddr = 0x6B;
constexpr uint8_t  kRegAdcControl = 0x02;    // bit 7 CONV_START, bit 6 CONV_RATE
constexpr uint8_t  kRegWatchdogRst = 0x03;   // bit 6 WD_RST
constexpr uint8_t  kRegBattVoltage = 0x0E;   // bits 6:0, 20 mV/LSB from 2.304 V
constexpr uint16_t kVbatBaseMv = 2304;
constexpr uint16_t kVbatStepMv = 20;
constexpr uint32_t kPeriodMs = 10000;

float    s_volts = 0;
uint32_t s_nextMs = 0;
bool     s_present = false;
bool     s_pending = false;

bool readReg(uint8_t reg, uint8_t &val) {
    Wire.beginTransmission(kAddr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)kAddr, 1) != 1) return false;
    val = Wire.read();
    return true;
}

bool writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(kAddr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

bool startConversion() {
    uint8_t adc = 0;
    if (!readReg(kRegAdcControl, adc)) return false;
    adc &= (uint8_t)~0x40;   // one shot
    adc |= 0x80;             // CONV_START
    return writeReg(kRegAdcControl, adc);
}

bool kickWatchdog() {
    uint8_t reg = 0;
    if (!readReg(kRegWatchdogRst, reg)) return false;
    return writeReg(kRegWatchdogRst, (uint8_t)(reg | 0x40));
}

// 0 = no fresh reading (keep the previous one).
float sample() {
    if (!s_present) {
        Wire.beginTransmission(kAddr);
        if (Wire.endTransmission() != 0) return 0;
        s_present = true;
        s_pending = false;
        Serial.println("[batt] BQ25896 found");
    }
    kickWatchdog();
    if (!s_pending) {
        s_pending = startConversion();
        if (!s_pending) s_present = false;
        return 0;
    }
    uint8_t adc = 0;
    if (!readReg(kRegAdcControl, adc)) {
        s_present = s_pending = false;
        return 0;
    }
    if (adc & 0x80) return 0;   // still converting
    uint8_t reg = 0;
    bool ok = readReg(kRegBattVoltage, reg);
    s_pending = startConversion();   // collected on the next sample
    if (!ok || (reg & 0x7F) == 0) return 0;
    return (kVbatBaseMv + (reg & 0x7F) * kVbatStepMv) / 1000.0f;
}
}  // namespace

void batteryBegin() {
    Wire.begin(I2C_SDA, I2C_SCL);
    s_nextMs = 0;
}

void batteryLoop(uint32_t nowMs) {
    if (s_nextMs && (int32_t)(nowMs - s_nextMs) < 0) return;
    // The first conversion is collected 1 s later rather than a full period.
    s_nextMs = nowMs + (s_volts <= 0 ? 1000 : kPeriodMs);
    float v = sample();
    if (v <= 0) return;
    s_volts = (s_volts <= 0.1f) ? v : s_volts * 0.7f + v * 0.3f;
}

float batteryVolts() { return s_volts; }
