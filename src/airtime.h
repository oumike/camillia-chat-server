#pragma once
// LoRa time-on-air and a rolling one-hour transmit budget for duty-cycle
// limited regions (spec §5.5). Pure C++, no Arduino.
#include <stdint.h>

// Semtech SX126x formula: explicit header, CRC on, LDRO when a symbol > 16 ms.
uint32_t timeOnAirMs(uint32_t payloadBytes, uint8_t sf, float bwKhz, uint8_t crDenom,
                     uint16_t preambleSymbols);

class AirtimeBudget {
public:
    static constexpr uint32_t WINDOW_MS = 3600000;
    // True if sending airMs now keeps the last hour's transmit time within limitPct.
    bool canSend(uint32_t nowMs, uint32_t airMs, uint8_t limitPct);
    void record(uint32_t nowMs, uint32_t airMs);
    // Transmit time in the last hour (expires old entries first).
    uint32_t usedMs(uint32_t nowMs);

private:
    static constexpr int CAP = 512;
    void expire(uint32_t nowMs);
    struct Tx { uint32_t atMs, airMs; };
    Tx       _tx[CAP];
    int      _start = 0, _count = 0;
    uint64_t _sum = 0;
};
