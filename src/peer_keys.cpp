#include "peer_keys.h"
#include <string.h>
#include "table_blob.h"

static constexpr uint32_t kMagic = 0x314B5043;   // "CPK1"

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
    _rev++;
}

size_t PeerKeys::toBlob(uint8_t *out, size_t cap, uint32_t nowMs) const {
    const size_t len = table_blob::HEADER + (size_t)_count * ENTRY_BLOB;
    if (!out || cap < len + table_blob::TRAILER) return 0;
    table_blob::writeHeader(out, kMagic, (uint32_t)_count);
    uint8_t *p = out + table_blob::HEADER;
    for (int i = 0; i < _count; i++, p += ENTRY_BLOB) {
        table_blob::put32(p, _e[i].id);
        table_blob::put32(p + 4, table_blob::ageOf(nowMs, _e[i].lastMs));
        memcpy(p + 8, _e[i].key, 32);
    }
    return table_blob::seal(out, len);
}

bool PeerKeys::fromBlob(const uint8_t *in, size_t len, uint32_t nowMs) {
    if (!_e) return false;
    const int count = table_blob::check(in, len, kMagic, ENTRY_BLOB, PEER_KEYS_CAP);
    if (count < 0) return false;
    const uint8_t *p = in + table_blob::HEADER;
    for (int i = 0; i < count; i++, p += ENTRY_BLOB) {
        _e[i].id = table_blob::get32(p);
        _e[i].lastMs = nowMs - table_blob::get32(p + 4);
        memcpy(_e[i].key, p + 8, 32);
    }
    _count = count;
    _rev++;
    return true;
}

const uint8_t *PeerKeys::find(uint32_t id) const {
    if (!_e) return nullptr;
    for (int i = 0; i < _count; i++) {
        if (_e[i].id == id) return _e[i].key;
    }
    return nullptr;
}
