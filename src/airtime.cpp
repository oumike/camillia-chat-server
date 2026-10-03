#include "airtime.h"
#include <math.h>

uint32_t timeOnAirMs(uint32_t pl, uint8_t sf, float bwKhz, uint8_t cr, uint16_t preamble) {
    const double tsym = (double)(1u << sf) / bwKhz;   // ms
    const int de = tsym > 16.0 ? 1 : 0;
    const double num = 8.0 * pl - 4.0 * sf + 28 + 16;
    double n = ceil(num / (4.0 * (sf - 2 * de))) * cr;
    if (n < 0) n = 0;
    return (uint32_t)lround((preamble + 4.25 + 8 + n) * tsym);
}

void AirtimeBudget::expire(uint32_t nowMs) {
    while (_count && nowMs - _tx[_start].atMs >= WINDOW_MS) {
        _sum -= _tx[_start].airMs;
        _start = (_start + 1) % CAP;
        _count--;
    }
}

bool AirtimeBudget::canSend(uint32_t nowMs, uint32_t airMs, uint8_t limitPct) {
    if (limitPct >= 100) return true;
    expire(nowMs);
    return _sum + airMs <= (uint64_t)WINDOW_MS * limitPct / 100;
}

void AirtimeBudget::record(uint32_t nowMs, uint32_t airMs) {
    expire(nowMs);
    if (_count == CAP) {
        // Log full: fold into the newest entry. It then ages out later than the
        // real packets would, so the budget can only over-count, never under-count.
        _tx[(_start + _count - 1) % CAP].airMs += airMs;
        _sum += airMs;
        return;
    }
    _tx[(_start + _count) % CAP] = {nowMs, airMs};
    _count++;
    _sum += airMs;
}
