// Wio Tracker L2 display, headless for now: dhalBegin always fails, so the
// server runs without a screen. The NV3031B / LP5814 / GT911 back end replaces this.
#include "display_hal.h"
#include <stdio.h>
#include "board.h"

bool dhalBegin(char *err, size_t cap) {
    snprintf(err, cap, "%s", boardExpanderOk() ? "wio display not implemented" : "expander not found");
    return false;
}

void dhalFlush(int32_t, int32_t, int32_t, int32_t, const uint16_t *) {}

bool dhalTouch(int16_t &, int16_t &) { return false; }

void dhalBacklight(uint8_t) {}

void dhalOff() {}

bool dhalWakePressed() { return false; }
