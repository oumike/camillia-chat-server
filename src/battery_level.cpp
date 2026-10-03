#include "battery_level.h"

namespace {
struct CurvePt { float v; uint8_t pct; };
const CurvePt kLiIonCurve[] = {
    {4.20f, 100}, {4.15f, 95}, {4.11f, 90}, {4.08f, 85},
    {4.02f, 75},  {3.98f, 65}, {3.95f, 55}, {3.91f, 45},
    {3.87f, 35},  {3.85f, 30}, {3.84f, 25}, {3.82f, 20},
    {3.80f, 15},  {3.79f, 10}, {3.77f, 5},  {3.73f, 2},
    {3.70f, 0},
};
constexpr int kLast = (int)(sizeof(kLiIonCurve) / sizeof(kLiIonCurve[0])) - 1;
}  // namespace

uint8_t batteryPercentFromVolts(float v) {
    if (v >= kLiIonCurve[0].v) return 100;
    if (v <= kLiIonCurve[kLast].v) return 0;
    for (int i = 0; i < kLast; i++) {
        const CurvePt &hi = kLiIonCurve[i], &lo = kLiIonCurve[i + 1];
        if (v <= hi.v && v >= lo.v) {
            float t = (v - lo.v) / (hi.v - lo.v);
            return (uint8_t)(lo.pct + (hi.pct - lo.pct) * t + 0.5f);
        }
    }
    return 0;
}

BatteryState batteryStateFromVolts(float v) {
    if (v < 2.5f) return BATT_ABSENT;
    if (v > 4.30f) return BATT_EXTERNAL;
    return BATT_PRESENT;
}
