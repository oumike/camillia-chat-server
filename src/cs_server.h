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
