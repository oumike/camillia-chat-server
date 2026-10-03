#include "page_cycler.h"

static constexpr int kPages = 3;

void PageCycler::begin(const CyclerConfig &cfg, uint32_t nowMs) {
    _cfg = cfg;
    _page = 0;
    _pageSince = nowMs;
    _wakeSince = nowMs;
    _external = true;
    _powerKnown = false;
    _dim = false;
    _level = _cfg.brightness;
}

void PageCycler::updateLevel(uint32_t nowMs) {
    _dim = !_external && _cfg.dimAfterSec != 0 &&
           (int32_t)(nowMs - _wakeSince) >= (int32_t)(_cfg.dimAfterSec * 1000u);
    _level = _dim ? _cfg.dimLevel : _cfg.brightness;
}

void PageCycler::tick(uint32_t nowMs, bool onExternalPower) {
    // begin() already counts as the wake; only a later true -> false edge restarts it.
    if (_powerKnown && _external && !onExternalPower) _wakeSince = nowMs;   // power just lost
    _external = onExternalPower;
    _powerKnown = true;

    // Catch up in whole steps so the cadence is kept even if ticks are sparse.
    uint32_t stepMs = (uint32_t)_cfg.pageSec * 1000u;
    if (stepMs && (int32_t)(nowMs - _pageSince) >= (int32_t)stepMs) {
        uint32_t steps = (uint32_t)(nowMs - _pageSince) / stepMs;
        _page = (_page + (int)(steps % kPages)) % kPages;
        _pageSince += steps * stepMs;
    }
    updateLevel(nowMs);
}

void PageCycler::tap(uint32_t nowMs) {
    if (_dim) {
        _dim = false;
    } else {
        _page = (_page + 1) % kPages;
    }
    _pageSince = nowMs;
    _wakeSince = nowMs;
    _level = _cfg.brightness;
}
