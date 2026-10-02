#pragma once
// Server-side protocol logic (spec §4.4–4.5). Pure C++, no Arduino.
#include <stddef.h>
#include <stdint.h>
#include "channel_store.h"
#include "cs_proto.h"

// Seq the reply should start *after*: cursor, then anchor, then everything.
uint32_t selectStart(const ChannelStore &store, const csp::Request &req);

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

struct ServerConfig {
    uint8_t  maxHops = 7;
    uint8_t  batchSize = 10;
    uint16_t packetGapMs = 3000;
    char     shortName[5] = "";
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
    void onPacket(uint32_t from, int chanSlot, uint8_t hopsTravelled,
                  const uint8_t *payload, size_t len, uint32_t nowMs);

    // At most one packet per call, and only once the packet gap has elapsed.
    bool poll(uint32_t nowMs, uint32_t nowUnix, bool timeValid, uint32_t nowUptimeSec,
              Outgoing &out);

    int  queueLength() const { return _qLen; }
    bool busy() const        { return _txIdx < _txCount; }

private:
    struct Pending { uint32_t from; int slot; uint8_t hops; csp::Request req; };
    struct AnnounceTo { uint32_t to; uint8_t hops; };

    void startNext(uint32_t nowUnix, bool timeValid, uint32_t nowUptimeSec);

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
    uint32_t _nextSendMs = 0;
};
