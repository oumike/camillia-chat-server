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

    // Which names have been heard from id: LONG_NAME | SHORT_NAME bits, 0 if none.
    static constexpr int LONG_NAME = 1, SHORT_NAME = 2;
    int nameParts(uint32_t id) const;

    // Occupied entries.
    int count() const { return _count; }

    // Bumped whenever an entry is created or changed: a saver compares it with
    // the revision it last wrote.
    uint32_t revision() const { return _rev; }

    // The table as a blob to save (0 if cap is too small), entries keeping their
    // age at nowMs. fromBlob() replaces the table with a saved one, the ages
    // counted back from nowMs; false, leaving the table as it was, for a corrupt
    // blob.
    static constexpr size_t ENTRY_BLOB = 4 + 4 + 25 + 5;
    static constexpr size_t BLOB_MAX = 8 + NODE_NAMES_CAP * ENTRY_BLOB + 4;
    size_t toBlob(uint8_t *out, size_t cap, uint32_t nowMs) const;
    bool fromBlob(const uint8_t *in, size_t len, uint32_t nowMs);

private:
    struct Entry {
        uint32_t id;
        uint32_t lastMs;
        char     longName[25];
        char     shortName[5];
    };

    Entry   *_e = nullptr;
    int      _count = 0;
    uint32_t _rev = 0;
};
