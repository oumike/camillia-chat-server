#include "peer_keys.h"
#include <string.h>

bool PeerKeys::begin(AllocFn alloc) {
    _count = 0;
    _e = static_cast<Entry *>(alloc(sizeof(Entry) * PEER_KEYS_CAP));
    return _e != nullptr;
}

void PeerKeys::update(uint32_t id, const uint8_t key[32], uint32_t nowMs) {
    if (!_e || !key) return;
    bool any = false;
    for (int i = 0; i < 32; i++) any |= key[i] != 0;
    if (!any) return;

    int idx = -1;
    for (int i = 0; i < _count; i++) {
        if (_e[i].id == id) { idx = i; break; }
    }
    if (idx < 0) {
        if (_count < PEER_KEYS_CAP) {
            idx = _count++;
        } else {
            idx = 0;
            for (int i = 1; i < _count; i++) {
                if (static_cast<int32_t>(_e[i].lastMs - _e[idx].lastMs) < 0) idx = i;
            }
        }
        _e[idx].id = id;
    }
    _e[idx].lastMs = nowMs;
    memcpy(_e[idx].key, key, 32);
}

const uint8_t *PeerKeys::find(uint32_t id) const {
    if (!_e) return nullptr;
    for (int i = 0; i < _count; i++) {
        if (_e[i].id == id) return _e[i].key;
    }
    return nullptr;
}
