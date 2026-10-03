#pragma once
// Node ID -> name table learned from NODEINFO (Heltec V4 expansion display).
// Fixed capacity, least-recently-updated entry evicted. Pure C++, no Arduino.
#include <stddef.h>
#include <stdint.h>

constexpr int NODE_NAMES_CAP = 200;

class NodeNames {
public:
    using AllocFn = void *(*)(size_t);

    // Allocates NODE_NAMES_CAP entries once; false if the allocation fails.
    bool begin(AllocFn alloc);

    // Long name cut to <= 24 bytes, short name to <= 4, both on a UTF-8
    // boundary. An empty/null name keeps the stored one. Both empty/null and
    // the ID unknown: no entry is created.
    void update(uint32_t id, const char *longName, const char *shortName, uint32_t nowMs);

    // Long name, else short name, else "!%08x". Always NUL-terminated within cap.
    void displayName(uint32_t id, char *out, size_t cap) const;

    // Occupied entries.
    int count() const { return _count; }

private:
    struct Entry {
        uint32_t id;
        uint32_t lastMs;
        char     longName[25];
        char     shortName[5];
    };

    Entry *_e = nullptr;
    int    _count = 0;
};
