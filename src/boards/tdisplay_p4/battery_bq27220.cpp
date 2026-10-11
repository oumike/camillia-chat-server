// T-Display P4 battery voltage from the BQ27220 fuel gauge on Wire (0x55),
// sampled every 10 s. Register handling follows camillia-mt 3777e74:
// src/battery_util.cpp (DEVICE_TDISPLAY_P4 block).
#include "battery.h"
#include <Arduino.h>
#include <Wire.h>
#include "board.h"

namespace {

constexpr uint32_t kPeriodMs = 10000;
constexpr uint32_t kReprobeMs = 60000;
constexpr uint8_t  kRegControl = 0x00;
constexpr uint8_t  kRegVoltage = 0x08;
constexpr uint8_t  kRegMacData = 0x40;
constexpr uint16_t kDeviceNumber = 0x0001;
constexpr uint16_t kExpectedDevice = 0x0220;

bool     s_found = false;
float    s_volts = 0;
uint32_t s_nextMs = 0;

bool readU16(uint8_t reg, uint16_t &value) {
    Wire.beginTransmission(BATT_FUEL_GAUGE_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)BATT_FUEL_GAUGE_ADDR, (size_t)2, true) != 2) return false;
    const uint8_t lo = (uint8_t)Wire.read();
    const uint8_t hi = (uint8_t)Wire.read();
    value = (uint16_t)lo | ((uint16_t)hi << 8);
    return true;
}

bool probe() {
    Wire.beginTransmission(BATT_FUEL_GAUGE_ADDR);
    Wire.write(kRegControl);
    Wire.write((uint8_t)kDeviceNumber);
    Wire.write((uint8_t)(kDeviceNumber >> 8));
    if (Wire.endTransmission() != 0) return false;
    delay(15);
    uint16_t device = 0;
    return readU16(kRegMacData, device) && device == kExpectedDevice;
}

}  // namespace

void batteryBegin() {
    // Wire is already up: boardEarlyInit() began it for the XL9535.
    s_nextMs = 0;
    s_found = probe();
    Serial.printf("[cs] battery: BQ27220 %s at 0x%02x\n", s_found ? "ok" : "not found",
                  BATT_FUEL_GAUGE_ADDR);
}

void batteryLoop(uint32_t nowMs) {
    if (s_nextMs && (int32_t)(nowMs - s_nextMs) < 0) return;
    if (!s_found) {
        // A gauge with no cell behind it may not answer at boot.
        s_nextMs = nowMs + kReprobeMs;
        s_found = probe();
        return;
    }
    s_nextMs = nowMs + kPeriodMs;
    uint16_t mv = 0;
    if (!readU16(kRegVoltage, mv)) return;
    const float v = mv / 1000.0f;
    // The gauge filters already; light smoothing only, as battery_adc.cpp does.
    s_volts = (s_volts <= 0.1f || v < 2.5f) ? v : s_volts * 0.7f + v * 0.3f;
}

float batteryVolts() { return s_volts; }
