#include "cs_server.h"
#include <string.h>

using namespace csp;

uint32_t selectStart(const ChannelStore &store, const Request &req) {
    const uint32_t everything = store.tailSeq() - 1;
    if (req.epoch == store.epoch() && req.cursor <= store.headSeq()) {
        return req.cursor > everything ? req.cursor : everything;
    }
    if (req.anchorFrom || req.anchorId) {
        uint32_t seq = store.findSeqAfterAnchor(req.anchorFrom, req.anchorId);
        if (seq) return seq;
    }
    return everything;
}

BatchPlan planBatch(const ChannelStore &store, uint32_t afterSeq, int batchSize, StoredMsg *out) {
    BatchPlan p{};
    p.count = store.copyAfter(afterSeq, out, batchSize);
    uint32_t lastSent = p.count ? out[p.count - 1].seq : afterSeq;
    p.more = lastSent < store.headSeq();
    return p;
}

static uint32_t ageOf(const StoredMsg &m, uint32_t nowUnix, bool timeValid, uint32_t nowUptimeSec) {
    if (timeValid && m.rxUnix && nowUnix >= m.rxUnix) return nowUnix - m.rxUnix;
    if (m.rxUptimeSec) return nowUptimeSec >= m.rxUptimeSec ? nowUptimeSec - m.rxUptimeSec : 0;
    return AGE_UNKNOWN;
}

int packItems(const StoredMsg *msgs, int n, uint32_t nowUnix, bool timeValid,
              uint32_t nowUptimeSec, uint32_t epoch, bool moreAfterBatch,
              uint8_t (*packets)[MAX_PAYLOAD], size_t *lens, int maxPackets) {
    BatchHeader h{};
    h.epoch = epoch;
    h.serverTime = timeValid ? nowUnix : 0;
    const uint8_t baseFlags = timeValid ? FLAG_TIME_VALID : 0;

    int np = 0, i = 0;
    do {
        if (np >= maxPackets) return -1;
        Item items[MAX_PAYLOAD / ITEM_OVERHEAD];
        uint8_t k = 0;
        size_t used = BATCH_HEADER;
        while (i < n) {
            const StoredMsg &m = msgs[i];
            size_t sz = ITEM_OVERHEAD + m.textLen;
            if (used + sz > MAX_PAYLOAD) break;
            Item &it = items[k++];
            it.seq = m.seq; it.from = m.from; it.packetId = m.packetId;
            it.ageSec = ageOf(m, nowUnix, timeValid, nowUptimeSec);
            it.textLen = m.textLen;
            memcpy(it.text, m.text, m.textLen);
            used += sz; i++;
        }
        h.flags = baseFlags;
        if (i >= n) h.flags |= FLAG_LAST | (moreAfterBatch ? FLAG_MORE : 0);
        h.count = k;
        lens[np] = encodeBatch(h, items, k, packets[np], MAX_PAYLOAD);
        np++;
    } while (i < n);
    return np;
}
