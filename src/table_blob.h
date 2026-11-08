#pragma once
// Shared framing for saving a node table (NodeNames, PeerKeys) as one blob:
// magic, entry count, the entries, then an FNV-1a checksum of everything before
// it. Entries carry their age, not lastMs: millis() restarts at boot. Pure C++.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace table_blob {

constexpr size_t HEADER = 8;    // magic (4) + count (4)
constexpr size_t TRAILER = 4;   // checksum

inline uint32_t fnv1a(const uint8_t *p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

inline void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
inline uint32_t get32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

// Age of an entry last updated at lastMs, kept below 2^31 so the signed
// comparisons that pick the least recent entry still hold after loading.
inline uint32_t ageOf(uint32_t nowMs, uint32_t lastMs) {
    uint32_t age = nowMs - lastMs;
    return age > 0x7FFFFFFFu ? 0x7FFFFFFFu : age;
}

inline void writeHeader(uint8_t *out, uint32_t magic, uint32_t count) {
    put32(out, magic);
    put32(out + 4, count);
}

// Appends the checksum after `len` bytes; returns the full length.
inline size_t seal(uint8_t *out, size_t len) {
    put32(out + len, fnv1a(out, len));
    return len + TRAILER;
}

// The entry count, or -1 if the blob is not a valid one of this magic, entry
// size and capacity.
inline int check(const uint8_t *in, size_t len, uint32_t magic, size_t entrySize, int cap) {
    if (!in || len < HEADER + TRAILER || get32(in) != magic) return -1;
    uint32_t count = get32(in + 4);
    if (count > (uint32_t)cap || len != HEADER + count * entrySize + TRAILER) return -1;
    if (get32(in + len - TRAILER) != fnv1a(in, len - TRAILER)) return -1;
    return (int)count;
}

}  // namespace table_blob
