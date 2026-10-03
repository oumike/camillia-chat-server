#pragma once
// Server-side protocol logic (spec §4.4–4.5). Pure C++, no Arduino.
#include <stddef.h>
#include <stdint.h>
#include <functional>
#include "channel_store.h"
#include "cs_proto.h"

// Seq the reply should start *after*: cursor, then anchor, then everything.
uint32_t selectStart(const ChannelStore &store, const csp::Request &req);

// Seconds since the server heard m, or csp::AGE_UNKNOWN (stored before a reboot, clock unset).
uint32_t messageAgeSec(const StoredMsg &m, uint32_t nowUnix, bool timeValid, uint32_t nowUptimeSec);

struct BatchPlan { int count; bool more; };
// Up to batchSize messages after afterSeq into out; `more` if any remain beyond them.
BatchPlan planBatch(const ChannelStore &store, uint32_t afterSeq, int batchSize, StoredMsg *out);

// Packs msgs (oldest first) greedily into BATCH payloads. The final packet gets
// FLAG_LAST, plus FLAG_MORE if moreAfterBatch. n == 0 still yields one empty
// LAST packet so the node knows it was answered. Returns the packet count, or
// -1 if maxPackets is too small.
int packItems(const StoredMsg *msgs, int n, uint32_t nowUnix, bool timeValid,
              uint32_t nowUptimeSec, uint32_t epoch, bool moreAfterBatch,
              uint8_t (*packets)[csp::MAX_PAYLOAD], size_t *lens, int maxPackets);

constexpr int CS_QUEUE_CAP      = 24;
constexpr int CS_ANNOUNCE_CAP   = 8;
constexpr int CS_MAX_BATCH      = 50;
constexpr int CS_RECENT_CAP     = 32;        // remembered (from, packetId) of port-256 packets
constexpr uint32_t CS_RECENT_MS = 600000;    // ...for 10 minutes

struct ServerConfig {
    uint8_t  maxHops = 7;
    uint8_t  batchSize = 10;
    uint16_t packetGapMs = 3000;
    char     shortName[5] = "";
    uint32_t myNodeId = 0;   // REQUESTs must be addressed to us
    // Called with the store slot just before a transfer's first packet is built.
    // The device saves a dirty store here, so every seq a node sees is on flash
    // and can never be reused for a different message after a power cut.
    std::function<void(int slot)> beforeServe;
};

struct Outgoing {
    uint32_t to;
    int      chanSlot;   // -1 = discovery channel
    uint8_t  hopLimit;
    uint8_t  payload[csp::MAX_PAYLOAD];
    size_t   len;
};

class CsServer {
public:
    void begin(const ServerConfig &cfg, ChannelStore *stores, const uint32_t *chanIds,
               const char (*chanNames)[12], int chanCount);

    // chanSlot: store slot the packet decrypted on, or -1 for the discovery channel.
    // REQUESTs must be addressed to us; DISCOVERs to us or broadcast. A packet
    // already seen (same from + packetId, e.g. a relay's copy) is ignored.
    void onPacket(uint32_t from, uint32_t to, uint32_t packetId, int chanSlot, uint8_t hopsTravelled,
                  const uint8_t *payload, size_t len, uint32_t nowMs);

    // At most one packet per call, and only once the packet gap has elapsed.
    bool poll(uint32_t nowMs, uint32_t nowUnix, bool timeValid, uint32_t nowUptimeSec,
              Outgoing &out);

    // Forget queued and in-progress replies for a store slot (its messages were cleared).
    void clearSlot(int slot);

    int  queueLength() const { return _qLen; }
    bool busy() const        { return _txIdx < _txCount; }

    // Time of the last BATCH packet sent to a node (announces don't count).
    bool lastSentAt(uint32_t &unix, uint32_t &uptimeSec) const {
        if (!_everSentBatch) return false;
        unix = _lastSentUnix; uptimeSec = _lastSentUptime;
        return true;
    }

private:
    struct Pending { uint32_t from; int slot; uint8_t hops; csp::Request req; };
    struct AnnounceTo { uint32_t to; uint8_t hops; };

    void startNext(uint32_t nowUnix, bool timeValid, uint32_t nowUptimeSec);
    bool seenRecently(uint32_t from, uint32_t packetId, uint32_t nowMs);

    struct Recent { uint32_t from, packetId, atMs; };
    Recent _recent[CS_RECENT_CAP];
    int    _recentNext = 0;
    int    _recentLen = 0;

    ServerConfig  _cfg;
    ChannelStore *_stores = nullptr;
    csp::Announce _announce{};
    int           _chanCount = 0;

    Pending    _q[CS_QUEUE_CAP];
    int        _qLen = 0;
    AnnounceTo _ann[CS_ANNOUNCE_CAP];
    int        _annLen = 0;

    // Current transfer. Members, not locals: ~23 KB would overflow the 8 KB loop stack.
    StoredMsg _msgs[CS_MAX_BATCH];
    uint32_t _txTo = 0;
    int      _txSlot = 0;
    uint8_t  _txHops = 0;
    uint8_t  _tx[CS_MAX_BATCH][csp::MAX_PAYLOAD];
    size_t   _txLen[CS_MAX_BATCH];
    int      _txCount = 0;
    int      _txIdx = 0;

    bool     _sentAny = false;
    bool     _everSentBatch = false;
    uint32_t _lastSentUnix = 0;
    uint32_t _lastSentUptime = 0;
    uint32_t _nextSendMs = 0;
};
