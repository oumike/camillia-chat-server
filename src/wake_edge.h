#pragma once
// Debounced press edge for the Wio Tracker L2's Wake button. Pure C++, no Arduino.
#include <stdint.h>

// True exactly once per press. `pressed` is the raw level (active = true).
// A level must be stable for >= 30 ms to count; held presses never repeat.
class WakeEdge {
public:
    bool update(bool pressed, uint32_t nowMs);
    bool busy() const { return _raw || _stable; }   // caller should keep sampling

private:
    bool     _raw = false;
    bool     _stable = false;
    uint32_t _changedMs = 0;
};
