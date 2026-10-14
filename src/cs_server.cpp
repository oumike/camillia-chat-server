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

uint32_t messageAgeSec(const StoredMsg &m, uint32_t nowUnix, bool timeValid, uint32_t nowUptimeSec) {
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
            it.ageSec = messageAgeSec(m, nowUnix, timeValid, nowUptimeSec);
            it.textLen = m.textLen;
            memcpy(it.text, m.text, m.textLen);
            used += sz; i++;
        }
        h.flags = baseFlags | (np == 0 ? FLAG_FIRST : 0);
        if (i >= n) h.flags |= FLAG_LAST | (moreAfterBatch ? FLAG_MORE : 0);
        h.count = k;
        lens[np] = encodeBatch(h, items, k, packets[np], MAX_PAYLOAD);
        np++;
    } while (i < n);
    return np;
}

// ── CsServer ─────────────────────────────────────────────────────

void CsServer::begin(const ServerConfig &cfg, ChannelStore *stores, const uint32_t *chanIds,
                     const char (*chanNames)[12], int chanCount) {
    _cfg = cfg;
    if (_cfg.batchSize < 1) _cfg.batchSize = 1;
    if (_cfg.batchSize > CS_MAX_BATCH) _cfg.batchSize = CS_MAX_BATCH;
    _stores = stores;
    _chanCount = chanCount < 0 ? 0 : chanCount > CS_MAX_CHANNELS ? CS_MAX_CHANNELS : chanCount;
    memset(_shortName, 0, sizeof _shortName);
    strncpy(_shortName, _cfg.shortName, sizeof(_shortName) - 1);
    memset(_chanNames, 0, sizeof _chanNames);
    for (int i = 0; i < _chanCount; i++) {
        _chanIds[i] = chanIds[i];
        strncpy(_chanNames[i], chanNames[i], sizeof(_chanNames[i]) - 1);
    }
    _qLen = _annLen = _txCount = _txIdx = 0;
    _recentLen = _recentNext = 0;
    _sentAny = false;
}

bool CsServer::seenRecently(uint32_t from, uint32_t packetId, uint32_t nowMs) {
    for (int i = 0; i < _recentLen; i++) {
        const Recent &r = _recent[i];
        if (r.from == from && r.packetId == packetId && nowMs - r.atMs < CS_RECENT_MS) return true;
    }
    _recent[_recentNext] = {from, packetId, nowMs};
    _recentNext = (_recentNext + 1) % CS_RECENT_CAP;
    if (_recentLen < CS_RECENT_CAP) _recentLen++;
    return false;
}

void CsServer::onPacket(uint32_t from, uint32_t to, uint32_t packetId, int chanSlot,
                        uint8_t hopsTravelled, const uint8_t *payload, size_t len, uint32_t nowMs) {
    if (hopsTravelled > _cfg.maxHops) return;
    Type t;
    if (!peekType(payload, len, t)) return;
    if (t == REQUEST && to != _cfg.myNodeId) return;
    if (t == DISCOVER && to != 0xFFFFFFFF && to != _cfg.myNodeId) return;
    if ((t == REQUEST || t == DISCOVER) && seenRecently(from, packetId, nowMs)) return;

    if (t == DISCOVER) {
        AnnounceTo a{};
        a.to = from;
        a.hops = hopsTravelled;
        if (!decodeDiscover(payload, len, a.ids, a.n, a.hasList)) return;
        for (int i = 0; i < _annLen; i++) {
            if (_ann[i].to == from) { _ann[i] = a; return; }   // newest list wins
        }
        if (_annLen < CS_ANNOUNCE_CAP) _ann[_annLen++] = a;
        return;
    }
    if (t != REQUEST || chanSlot < 0 || chanSlot >= _chanCount) return;

    Request req;
    if (!decodeRequest(payload, len, req)) return;
    if (busy() && _txTo == from && _txSlot == chanSlot) return;   // already being served
    Pending p{from, chanSlot, hopsTravelled, req};
    for (int i = 0; i < _qLen; i++) {
        if (_q[i].from == from && _q[i].slot == chanSlot) { _q[i] = p; return; }
    }
    if (_qLen < CS_QUEUE_CAP) _q[_qLen++] = p;
}

void CsServer::startNext(uint32_t nowUnix, bool timeValid, uint32_t nowUptimeSec) {
    Pending p = _q[0];
    memmove(_q, _q + 1, sizeof(Pending) * (size_t)(_qLen - 1));
    _qLen--;

    if (_cfg.beforeServe) _cfg.beforeServe(p.slot);
    const ChannelStore &store = _stores[p.slot];
    BatchPlan plan = planBatch(store, selectStart(store, p.req), _cfg.batchSize, _msgs);
    _txCount = packItems(_msgs, plan.count, nowUnix, timeValid, nowUptimeSec, store.epoch(),
                         plan.more, _tx, _txLen, CS_MAX_BATCH);
    if (_txCount < 0) _txCount = 0;
    _txIdx = 0;
    _txTo = p.from;
    _txSlot = p.slot;
    _txHops = p.hops;
}

// Shared channels in store order. A client that sent no list gets the first
// MAX_ANNOUNCE_CHANNELS, as before the list existed.
void CsServer::buildAnnounce(const AnnounceTo &a, Announce &out) const {
    memset(&out, 0, sizeof out);
    memcpy(out.shortName, _shortName, sizeof out.shortName);
    for (int i = 0; i < _chanCount && out.count < MAX_ANNOUNCE_CHANNELS; i++) {
        bool shared = !a.hasList;
        for (uint8_t k = 0; k < a.n && !shared; k++) shared = a.ids[k] == _chanIds[i];
        if (!shared) continue;
        out.ch[out.count].id = _chanIds[i];
        memcpy(out.ch[out.count].name, _chanNames[i], sizeof out.ch[out.count].name);
        out.count++;
    }
}

bool CsServer::announceBroadcast() {
    AnnounceTo a{};
    a.to = 0xFFFFFFFF;
    a.hops = _cfg.maxHops;
    for (int i = 0; i < _annLen; i++) {
        if (_ann[i].to == a.to) return true;   // one already waiting
    }
    if (_annLen >= CS_ANNOUNCE_CAP) return false;
    _ann[_annLen++] = a;
    return true;
}

void CsServer::clearSlot(int slot) {
    int w = 0;
    for (int i = 0; i < _qLen; i++) if (_q[i].slot != slot) _q[w++] = _q[i];
    _qLen = w;
    if (busy() && _txSlot == slot) _txIdx = _txCount = 0;
}

bool CsServer::poll(uint32_t nowMs, uint32_t nowUnix, bool timeValid, uint32_t nowUptimeSec,
                    Outgoing &out) {
    if (_sentAny && (int32_t)(nowMs - _nextSendMs) < 0) return false;

    if (_annLen) {
        AnnounceTo a = _ann[0];
        memmove(_ann, _ann + 1, sizeof(AnnounceTo) * (size_t)(_annLen - 1));
        _annLen--;
        out.to = a.to;
        out.chanSlot = -1;
        out.hopLimit = a.hops;
        Announce ann;
        buildAnnounce(a, ann);
        out.len = encodeAnnounce(ann, out.payload, sizeof(out.payload));
    } else {
        if (!busy()) {
            if (!_qLen) return false;
            startNext(nowUnix, timeValid, nowUptimeSec);
            if (!busy()) return false;
        }
        out.to = _txTo;
        out.chanSlot = _txSlot;
        out.hopLimit = _txHops;
        out.len = _txLen[_txIdx];
        memcpy(out.payload, _tx[_txIdx], out.len);
        _txIdx++;
        _everSentBatch = true;
        _lastSentUnix = timeValid ? nowUnix : 0;
        _lastSentUptime = nowUptimeSec;
    }
    _sentAny = true;
    _nextSendMs = nowMs + _cfg.packetGapMs;
    return true;
}
