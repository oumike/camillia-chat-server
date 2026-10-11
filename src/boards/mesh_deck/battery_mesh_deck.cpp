// Battery for the Attaky Mesh Deck (mesh-deck): the Standard Cell's MAX17048
// fuel gauge on the main I2C bus — there is no ADC divider. VCELL (0x02) is
// 78.125 uV per LSB. Register access is camillia-mt 3777e74
// src/battery_util.cpp (DEVICE_MESH_DECK: maxReadReg16 / batteryReadMeshDeckVolts).
// The gauge's own SOC is not used: the chat server derives percentage from
// volts (battery_level.h) on every board.
#include "battery.h"
#include <Arduino.h>
#include <Wire.h>
#include "board.h"

namespace {

constexpr uint32_t kPeriodMs = 10000;
constexpr uint8_t  kRegVCell = 0x02;

float    s_volts = 0;
uint32_t s_nextMs = 0;
bool     s_reportedMissing = false;

bool readReg16(uint8_t reg, uint16_t &out) {
    Wire.beginTransmission(BATT_FUEL_GAUGE_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)BATT_FUEL_GAUGE_ADDR, 2) != 2) return false;
    const uint8_t hi = (uint8_t)Wire.read();
    const uint8_t lo = (uint8_t)Wire.read();
    out = (uint16_t)((hi << 8) | lo);
    return true;
}

}  // namespace

void batteryBegin() { s_nextMs = 0; }   // Wire is up from boardEarlyInit()

void batteryLoop(uint32_t nowMs) {
    if (s_nextMs && (int32_t)(nowMs - s_nextMs) < 0) return;
    s_nextMs = nowMs + kPeriodMs;
    uint16_t raw = 0;
    if (!readReg16(kRegVCell, raw)) {
        if (!s_reportedMissing) {
            s_reportedMissing = true;
            Serial.printf("[batt] MAX17048 not answering at 0x%02x\n", BATT_FUEL_GAUGE_ADDR);
        }
        s_volts = 0;
        return;
    }
    const float v = raw * 0.000078125f;
    s_volts = (s_volts <= 0.1f || v < 2.5f) ? v : s_volts * 0.7f + v * 0.3f;
}

float batteryVolts() { return s_volts; }
