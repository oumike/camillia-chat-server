#pragma once
// One monitored channel's message history (spec §5.3). Pure C++, no Arduino.
#include <stddef.h>
#include <stdint.h>

constexpr int CS_MAX_CHANNELS     = 10;
constexpr int CS_MSGS_PER_CHANNEL = 250;
constexpr int CS_MAX_TEXT         = 200;

enum MsgSource : uint8_t { SRC_LORA = 0, SRC_MQTT = 1 };

struct StoredMsg {
    uint32_t seq;
    uint32_t from;
    uint32_t packetId;
    uint32_t rxUnix;       // 0 = server clock was not set when heard
    uint32_t rxUptimeSec;  // >= 1 when heard this boot; 0 = loaded from a previous boot
    uint8_t  source;       // MsgSource
    uint8_t  textLen;
    char     text[CS_MAX_TEXT + 1];
};

class ChannelStore {
public:
    using AllocFn  = void *(*)(size_t);
    using RandomFn = uint32_t (*)();

    // Allocates the ring once and starts empty with a fresh random epoch.
    bool begin(AllocFn alloc, RandomFn random);

    // Returns false for a duplicate (same from + packetId still held).
    // Text is cut to <= 200 bytes on a UTF-8 boundary.
    bool add(uint32_t from, uint32_t packetId, const char *text, size_t len,
             uint32_t rxUnix, uint32_t rxUptimeSec, uint8_t source);

    uint32_t epoch() const   { return _epoch; }
    // Newest / oldest held seq. Seqs are contiguous (deserialize also accepts gaps).
    uint32_t headSeq() const { return _count ? at(_count - 1).seq : _lastSeq; }
    uint32_t tailSeq() const { return _count ? at(0).seq : _lastSeq + 1; }
    int      count() const   { return _count; }

    // Seq of the held message (from, packetId), or 0 if not held.
    uint32_t findSeqAfterAnchor(uint32_t from, uint32_t packetId) const;
    // Messages with seq > afterSeq, oldest first, at most `max`.
    int copyAfter(uint32_t afterSeq, StoredMsg *out, int max) const;

    // Newest held message with seq < beforeSeq; false if none.
    bool copyBefore(uint32_t beforeSeq, StoredMsg *out) const;

    void reset(uint32_t newEpoch);

    // Bytes of message text held, and the ring's fixed allocation.
    size_t textBytes() const;
    static size_t ramBytes() { return sizeof(StoredMsg) * CS_MSGS_PER_CHANNEL; }

    bool     dirty() const         { return _dirty; }
    uint32_t addsSinceSave() const { return _addsSinceSave; }
    void     markSaved()           { _dirty = false; _addsSinceSave = 0; }

    // "CSS1" | epoch | lastSeq | count(u16) | records | CRC32. deserialize()
    // leaves the store untouched and returns false on any inconsistency.
    static size_t maxSerializedSize();
    size_t serialize(uint8_t *out, size_t cap) const;
    bool   deserialize(const uint8_t *in, size_t len);

private:
    const StoredMsg &at(int i) const { return _ring[(_start + i) % CS_MSGS_PER_CHANNEL]; }

    StoredMsg *_ring = nullptr;
    RandomFn   _random = nullptr;
    int        _start = 0;
    int        _count = 0;
    uint32_t   _lastSeq = 0;
    uint32_t   _epoch = 0;
    bool       _dirty = false;
    uint32_t   _addsSinceSave = 0;
};

// Newest messages across stores, newest first. A message heard this boot
// (rxUptimeSec > 0) is newer than one from a previous boot; two this-boot
// messages compare by rxUptimeSec; two previous-boot ones by rxUnix, then seq.
// slots[i] (may be nullptr) is the store index out[i] came from.
int newestAcross(const ChannelStore *stores, int count, StoredMsg *out, int8_t *slots, int max);
