#include "wake_edge.h"

namespace {
constexpr uint32_t kDebounceMs = 30;
}

bool WakeEdge::update(bool pressed, uint32_t nowMs) {
    if (pressed != _raw) {
        _raw = pressed;
        _changedMs = nowMs;
    }
    if ((uint32_t)(nowMs - _changedMs) < kDebounceMs) return false;
    if (_stable == _raw) return false;
    _stable = _raw;
    return _stable;
}
