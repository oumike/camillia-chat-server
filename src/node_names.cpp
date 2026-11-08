#include "node_names.h"
#include <stdio.h>
#include <string.h>
#include "table_blob.h"

static constexpr uint32_t kMagic = 0x314E4E43;   // "CNN1"

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
    _rev++;
}

int NodeNames::nameParts(uint32_t id) const {
    for (int i = 0; i < _count; i++) {
        if (_e[i].id != id) continue;
        return (_e[i].longName[0] ? LONG_NAME : 0) | (_e[i].shortName[0] ? SHORT_NAME : 0);
    }
    return 0;
}

size_t NodeNames::toBlob(uint8_t *out, size_t cap, uint32_t nowMs) const {
    const size_t len = table_blob::HEADER + (size_t)_count * ENTRY_BLOB;
    if (!out || cap < len + table_blob::TRAILER) return 0;
    table_blob::writeHeader(out, kMagic, (uint32_t)_count);
    uint8_t *p = out + table_blob::HEADER;
    for (int i = 0; i < _count; i++, p += ENTRY_BLOB) {
        table_blob::put32(p, _e[i].id);
        table_blob::put32(p + 4, table_blob::ageOf(nowMs, _e[i].lastMs));
        memcpy(p + 8, _e[i].longName, sizeof _e[i].longName);
        memcpy(p + 8 + sizeof _e[i].longName, _e[i].shortName, sizeof _e[i].shortName);
    }
    return table_blob::seal(out, len);
}

bool NodeNames::fromBlob(const uint8_t *in, size_t len, uint32_t nowMs) {
    if (!_e) return false;
    const int count = table_blob::check(in, len, kMagic, ENTRY_BLOB, NODE_NAMES_CAP);
    if (count < 0) return false;
    const uint8_t *p = in + table_blob::HEADER;
    for (int i = 0; i < count; i++, p += ENTRY_BLOB) {
        Entry &e = _e[i];
        e.id = table_blob::get32(p);
        e.lastMs = nowMs - table_blob::get32(p + 4);
        memcpy(e.longName, p + 8, sizeof e.longName);
        memcpy(e.shortName, p + 8 + sizeof e.longName, sizeof e.shortName);
        e.longName[sizeof e.longName - 1] = 0;
        e.shortName[sizeof e.shortName - 1] = 0;
    }
    _count = count;
    _rev++;
    return true;
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
