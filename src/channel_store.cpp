#include "channel_store.h"
#include <string.h>

namespace {

constexpr uint8_t kMagic[4] = {'C', 'S', 'S', '1'};
constexpr size_t  kHeader = 4 + 4 + 4 + 2;
constexpr size_t  kRecordFixed = 4 * 4 + 1 + 1;   // seq, from, packetId, rxUnix, source, textLen

uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFF;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & (0u - (c & 1)));
    }
    return ~c;
}

void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// Longest prefix of at most `max` bytes that does not end mid-codepoint.
size_t utf8Cut(const char *s, size_t len, size_t max) {
    if (len <= max) return len;
    size_t n = max;
    while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
    return n;
}

}  // namespace

bool ChannelStore::begin(AllocFn alloc, RandomFn random) {
    _ring = (StoredMsg *)alloc(sizeof(StoredMsg) * CS_MSGS_PER_CHANNEL);
    _random = random;
    if (!_ring) return false;
    reset(_random());
    _dirty = false;
    return true;
}

bool ChannelStore::add(uint32_t from, uint32_t packetId, const char *text, size_t len,
                       uint32_t rxUnix, uint32_t rxUptimeSec, uint8_t source) {
    if (findSeqAfterAnchor(from, packetId)) return false;

    int slot;
    if (_count < CS_MSGS_PER_CHANNEL) {
        slot = (_start + _count) % CS_MSGS_PER_CHANNEL;
        _count++;
    } else {
        slot = _start;
        _start = (_start + 1) % CS_MSGS_PER_CHANNEL;
    }
    StoredMsg &m = _ring[slot];
    m.seq = ++_lastSeq;
    m.from = from;
    m.packetId = packetId;
    m.rxUnix = rxUnix;
    m.rxUptimeSec = rxUptimeSec ? rxUptimeSec : 1;
    m.source = source;
    m.textLen = (uint8_t)utf8Cut(text, len, CS_MAX_TEXT);
    memcpy(m.text, text, m.textLen);
    m.text[m.textLen] = 0;

    _dirty = true;
    _addsSinceSave++;
    return true;
}

uint32_t ChannelStore::findSeqAfterAnchor(uint32_t from, uint32_t packetId) const {
    for (int i = 0; i < _count; i++) {
        const StoredMsg &m = at(i);
        if (m.from == from && m.packetId == packetId) return m.seq;
    }
    return 0;
}

int ChannelStore::copyAfter(uint32_t afterSeq, StoredMsg *out, int max) const {
    int n = 0;
    for (int i = 0; i < _count && n < max; i++) {
        const StoredMsg &m = at(i);
        if (m.seq > afterSeq) out[n++] = m;
    }
    return n;
}

size_t ChannelStore::textBytes() const {
    size_t n = 0;
    for (int i = 0; i < _count; i++) n += at(i).textLen;
    return n;
}

void ChannelStore::reset(uint32_t newEpoch) {
    _start = 0;
    _count = 0;
    _lastSeq = 0;
    _epoch = newEpoch;
    _dirty = true;
    _addsSinceSave = 0;
}

size_t ChannelStore::maxSerializedSize() {
    return kHeader + (size_t)CS_MSGS_PER_CHANNEL * (kRecordFixed + CS_MAX_TEXT) + 4;
}

size_t ChannelStore::serialize(uint8_t *out, size_t cap) const {
    size_t need = kHeader + 4;
    for (int i = 0; i < _count; i++) need += kRecordFixed + at(i).textLen;
    if (need > cap) return 0;

    uint8_t *p = out;
    memcpy(p, kMagic, 4); p += 4;
    put32(p, _epoch); p += 4;
    put32(p, _lastSeq); p += 4;
    p[0] = (uint8_t)_count; p[1] = (uint8_t)(_count >> 8); p += 2;
    for (int i = 0; i < _count; i++) {
        const StoredMsg &m = at(i);
        put32(p, m.seq); put32(p + 4, m.from); put32(p + 8, m.packetId); put32(p + 12, m.rxUnix);
        p[16] = m.source; p[17] = m.textLen; p += kRecordFixed;
        memcpy(p, m.text, m.textLen); p += m.textLen;
    }
    put32(p, crc32(out, (size_t)(p - out))); p += 4;
    return (size_t)(p - out);
}

bool ChannelStore::deserialize(const uint8_t *in, size_t len) {
    if (!_ring || len < kHeader + 4 || memcmp(in, kMagic, 4) != 0) return false;
    if (crc32(in, len - 4) != get32(in + len - 4)) return false;

    uint32_t epoch = get32(in + 4);
    uint32_t lastSeq = get32(in + 8);
    int count = in[12] | in[13] << 8;
    if (count > CS_MSGS_PER_CHANNEL || (uint32_t)count > lastSeq) return false;

    // Validate everything before touching the ring: seqs strictly increase, end <= lastSeq.
    size_t off = kHeader;
    uint32_t prevSeq = 0;
    for (int i = 0; i < count; i++) {
        if (off + kRecordFixed > len - 4) return false;
        uint32_t seq = get32(in + off);
        uint8_t tl = in[off + 17];
        if (seq <= prevSeq || seq > lastSeq || tl > CS_MAX_TEXT) return false;
        prevSeq = seq;
        off += kRecordFixed + tl;
    }
    if (off != len - 4) return false;

    off = kHeader;
    for (int i = 0; i < count; i++) {
        StoredMsg &m = _ring[i];
        m.seq = get32(in + off); m.from = get32(in + off + 4);
        m.packetId = get32(in + off + 8); m.rxUnix = get32(in + off + 12);
        m.rxUptimeSec = 0;
        m.source = in[off + 16]; m.textLen = in[off + 17];
        off += kRecordFixed;
        memcpy(m.text, in + off, m.textLen);
        m.text[m.textLen] = 0;
        off += m.textLen;
    }
    _start = 0;
    _count = count;
    _lastSeq = lastSeq + CS_SEQ_RESERVE;
    _epoch = epoch;
    _dirty = false;
    _addsSinceSave = 0;
    return true;
}
