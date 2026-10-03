#include "node_names.h"
#include <stdio.h>
#include <string.h>

bool NodeNames::begin(AllocFn alloc) {
    _count = 0;
    _e = static_cast<Entry *>(alloc(sizeof(Entry) * NODE_NAMES_CAP));
    return _e != nullptr;
}

// Copies at most maxBytes of src, never splitting a UTF-8 character.
static void copyUtf8(char *dst, size_t maxBytes, const char *src) {
    size_t n = strnlen(src, maxBytes + 1);
    if (n > maxBytes) {
        n = maxBytes;
        while (n > 0 && (static_cast<uint8_t>(src[n]) & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void NodeNames::update(uint32_t id, const char *longName, const char *shortName,
                       uint32_t nowMs) {
    if (!_e) return;
    bool hasLong  = longName && longName[0];
    bool hasShort = shortName && shortName[0];

    int idx = -1;
    for (int i = 0; i < _count; i++) {
        if (_e[i].id == id) { idx = i; break; }
    }
    if (idx < 0) {
        if (!hasLong && !hasShort) return;
        if (_count < NODE_NAMES_CAP) {
            idx = _count++;
        } else {
            idx = 0;
            for (int i = 1; i < _count; i++) {
                if (static_cast<int32_t>(_e[i].lastMs - _e[idx].lastMs) < 0) idx = i;
            }
        }
        memset(&_e[idx], 0, sizeof(Entry));
        _e[idx].id = id;
    }
    _e[idx].lastMs = nowMs;
    if (hasLong)  copyUtf8(_e[idx].longName, sizeof(_e[idx].longName) - 1, longName);
    if (hasShort) copyUtf8(_e[idx].shortName, sizeof(_e[idx].shortName) - 1, shortName);
}

void NodeNames::displayName(uint32_t id, char *out, size_t cap) const {
    if (!out || cap == 0) return;
    for (int i = 0; i < _count; i++) {
        if (_e[i].id != id) continue;
        const char *s = _e[i].longName[0] ? _e[i].longName
                      : _e[i].shortName[0] ? _e[i].shortName : nullptr;
        if (s) {
            snprintf(out, cap, "%s", s);
            return;
        }
        break;
    }
    snprintf(out, cap, "!%08x", static_cast<unsigned>(id));
}
