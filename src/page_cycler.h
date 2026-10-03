#pragma once
// Page rotation and backlight dimming state machine (spec §5.3). Pure C++, no Arduino.
#include <stdint.h>

struct CyclerConfig { uint16_t pageSec; uint16_t dimAfterSec; uint8_t brightness; uint8_t dimLevel; };

class PageCycler {
public:
    void    begin(const CyclerConfig &cfg, uint32_t nowMs);
    void    tick(uint32_t nowMs, bool onExternalPower);   // advances on timeout, updates the dim state
    void    tap(uint32_t nowMs);                          // wake if dimmed, else advance
    int     page() const      { return _page; }           // 0, 1, 2
    uint8_t backlight() const { return _level; }          // level to write to the PWM

private:
    void updateLevel(uint32_t nowMs);

    CyclerConfig _cfg = {30, 120, 160, 0};
    int      _page = 0;
    uint8_t  _level = 160;
    uint32_t _pageSince = 0;   // last page change (timer or tap)
    uint32_t _wakeSince = 0;   // last tap, boot, or loss of external power
    bool     _external = true;
    bool     _powerKnown = false;   // false until the first tick after begin()
    bool     _dim = false;
};
