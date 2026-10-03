#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cs_server.h"

using namespace csp;

static uint32_t s_rand = 500;
static uint32_t fakeRandom() { return ++s_rand; }

static ChannelStore *storeWith(int n, int textLen = 5) {
    ChannelStore *s = new ChannelStore();
    s->begin(malloc, fakeRandom);
    char text[CS_MAX_TEXT + 1];
    memset(text, 't', textLen); text[textLen] = 0;
    for (int i = 1; i <= n; i++) s->add(0xAB, (uint32_t)i, text, textLen, 1700000000, 5, 0);
    return s;
}

void setUp() {}
void tearDown() {}

// ── Task 4: since-selection and batch packing ───────────────────

void test_since_uses_cursor_when_epoch_matches() {
    ChannelStore *s = storeWith(10);
    Request r{s->epoch(), 6, 0xAB, 2};
    TEST_ASSERT_EQUAL_UINT32(6, selectStart(*s, r));
    delete s;
}

void test_since_uses_anchor_when_epoch_differs() {
    ChannelStore *s = storeWith(10);
    Request r{s->epoch() + 1, 6, 0xAB, 3};
    TEST_ASSERT_EQUAL_UINT32(3, selectStart(*s, r));
    delete s;
}

void test_since_everything_when_nothing_matches() {
    ChannelStore *s = storeWith(10);
    Request r{0, 0, 0x99, 77};
    TEST_ASSERT_EQUAL_UINT32(s->tailSeq() - 1, selectStart(*s, r));
    Request none{0, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT32(s->tailSeq() - 1, selectStart(*s, none));
    delete s;
}

void test_since_cursor_beyond_head_falls_back() {
    ChannelStore *s = storeWith(10);
    Request r{s->epoch(), s->headSeq() + 5, 0xAB, 4};
    TEST_ASSERT_EQUAL_UINT32(4, selectStart(*s, r));   // anchor
    Request r2{s->epoch(), s->headSeq() + 5, 0, 0};
    TEST_ASSERT_EQUAL_UINT32(s->tailSeq() - 1, selectStart(*s, r2));  // everything
    delete s;
}

void test_since_cursor_older_than_tail_sends_everything_held() {
    ChannelStore *s = storeWith(260);   // tail is 11
    Request r{s->epoch(), 3, 0, 0};
    TEST_ASSERT_EQUAL_UINT32(10, selectStart(*s, r));
    delete s;
}

void test_plan_batch_sets_more() {
    ChannelStore *s = storeWith(25);
    StoredMsg out[10];
    BatchPlan p = planBatch(*s, 0, 10, out);
    TEST_ASSERT_EQUAL(10, p.count);
    TEST_ASSERT_TRUE(p.more);
    p = planBatch(*s, out[9].seq, 10, out);
    TEST_ASSERT_EQUAL(10, p.count);
    TEST_ASSERT_TRUE(p.more);
    p = planBatch(*s, out[9].seq, 10, out);
    TEST_ASSERT_EQUAL(5, p.count);
    TEST_ASSERT_FALSE(p.more);
    delete s;
}

void test_pack_respects_payload_limit() {
    ChannelStore *s = storeWith(10, 150);
    StoredMsg msgs[10];
    s->copyAfter(0, msgs, 10);
    uint8_t packets[16][MAX_PAYLOAD];
    size_t lens[16];
    int np = packItems(msgs, 10, 1700000100, true, 100, s->epoch(), true, packets, lens, 16);
    TEST_ASSERT_EQUAL(10, np);   // 150-byte texts do not pair up
    uint32_t nextSeq = 1;
    for (int i = 0; i < np; i++) {
        TEST_ASSERT_LESS_OR_EQUAL(MAX_PAYLOAD, lens[i]);
        BatchHeader h; Item items[8]; uint8_t n;
        TEST_ASSERT_TRUE(decodeBatch(packets[i], lens[i], h, items, 8, n));
        for (int k = 0; k < n; k++) TEST_ASSERT_EQUAL_UINT32(nextSeq++, items[k].seq);
        bool last = (i == np - 1);
        TEST_ASSERT_EQUAL(last, (h.flags & FLAG_LAST) != 0);
        TEST_ASSERT_EQUAL(last, (h.flags & FLAG_MORE) != 0);
        TEST_ASSERT_TRUE(h.flags & FLAG_TIME_VALID);
        TEST_ASSERT_EQUAL_UINT32(1700000100, h.serverTime);
        TEST_ASSERT_EQUAL_UINT32(100, items[0].ageSec);
    }
    TEST_ASSERT_EQUAL_UINT32(11, nextSeq);
    delete s;
}

void test_pack_short_messages_share_a_packet() {
    ChannelStore *s = storeWith(10, 4);   // 12 + 10 x (17 + 4) = 222 bytes
    StoredMsg msgs[10];
    s->copyAfter(0, msgs, 10);
    uint8_t packets[16][MAX_PAYLOAD];
    size_t lens[16];
    TEST_ASSERT_EQUAL(1, packItems(msgs, 10, 0, false, 0, 1, false, packets, lens, 16));
    delete s;
}

void test_pack_age_unknown_after_reboot() {
    StoredMsg m{};
    m.seq = 1; m.rxUnix = 0; m.rxUptimeSec = 0; m.textLen = 1; m.text[0] = 'x';
    StoredMsg live = m;
    live.seq = 2; live.rxUptimeSec = 40;
    StoredMsg both[2] = {m, live};
    uint8_t packets[2][MAX_PAYLOAD];
    size_t lens[2];
    TEST_ASSERT_EQUAL(1, packItems(both, 2, 0, false, 100, 1, false, packets, lens, 2));
    BatchHeader h; Item items[2]; uint8_t n;
    TEST_ASSERT_TRUE(decodeBatch(packets[0], lens[0], h, items, 2, n));
    TEST_ASSERT_EQUAL_UINT32(AGE_UNKNOWN, items[0].ageSec);
    TEST_ASSERT_EQUAL_UINT32(60, items[1].ageSec);
    TEST_ASSERT_FALSE(h.flags & FLAG_TIME_VALID);
}

void test_pack_empty_sends_one_last_packet() {
    uint8_t packets[1][MAX_PAYLOAD];
    size_t lens[1];
    TEST_ASSERT_EQUAL(1, packItems(nullptr, 0, 0, false, 0, 9, false, packets, lens, 1));
    BatchHeader h; Item items[1]; uint8_t n;
    TEST_ASSERT_TRUE(decodeBatch(packets[0], lens[0], h, items, 1, n));
    TEST_ASSERT_EQUAL(0, n);
    TEST_ASSERT_EQUAL(FLAG_LAST, h.flags);
    TEST_ASSERT_EQUAL_UINT32(9, h.epoch);
}

void test_pack_never_exceeds_radio_limit() {
    // Two items totalling 187 bytes of text: 12 + 2*17 + 187 = 233 would fit the
    // old 233 limit but not the radio (255 - 16 header - 8 Data framing = 231).
    StoredMsg m[2] = {};
    m[0].seq = 1; m[0].textLen = 100; memset(m[0].text, 'a', 100);
    m[1].seq = 2; m[1].textLen = 87;  memset(m[1].text, 'b', 87);
    uint8_t packets[4][MAX_PAYLOAD];
    size_t lens[4];
    int np = packItems(m, 2, 0, false, 0, 1, false, packets, lens, 4);
    TEST_ASSERT_EQUAL(2, np);
    for (int i = 0; i < np; i++) TEST_ASSERT_LESS_OR_EQUAL(231, lens[i]);
}

// ── Task 5: CsServer queue, hops, pacing ─────────────────────────

static constexpr uint32_t ME = 0x5E5E5E5E;
static uint32_t s_pktId = 1;

struct Rig {
    ChannelStore *stores;
    uint32_t ids[3] = {0x11111111, 0x22222222, 0x33333333};
    char names[3][12] = {"LongFast", "camillia", "third"};
    CsServer server;
    Rig(int msgs, int textLen, uint8_t maxHops = 7, uint8_t batch = 10, uint16_t gap = 3000) {
        stores = new ChannelStore[3];
        for (int c = 0; c < 3; c++) stores[c].begin(malloc, fakeRandom);
        char text[CS_MAX_TEXT + 1];
        memset(text, 'q', textLen); text[textLen] = 0;
        for (int i = 1; i <= msgs; i++) stores[0].add(0xAB, (uint32_t)i, text, textLen, 0, 5, 0);
        ServerConfig cfg;
        cfg.maxHops = maxHops; cfg.batchSize = batch; cfg.packetGapMs = gap;
        strcpy(cfg.shortName, "CSRV");
        cfg.myNodeId = ME;
        server.begin(cfg, stores, ids, names, 2);
    }
    ~Rig() { delete[] stores; }
    void request(uint32_t from, int slot, uint8_t hops, uint32_t cursor, uint32_t nowMs = 0,
                 uint32_t to = ME, uint32_t pktId = 0) {
        Request r{stores[slot].epoch(), cursor, 0, 0};
        uint8_t buf[MAX_PAYLOAD];
        size_t n = encodeRequest(r, buf, sizeof buf);
        server.onPacket(from, to, pktId ? pktId : s_pktId++, slot, hops, buf, n, nowMs);
    }
    void discover(uint32_t from, uint8_t hops, uint32_t to = 0xFFFFFFFF, uint32_t pktId = 0) {
        uint8_t buf[4];
        size_t n = encodeDiscover(buf, sizeof buf);
        server.onPacket(from, to, pktId ? pktId : s_pktId++, -1, hops, buf, n, 0);
    }
    bool poll(uint32_t nowMs, Outgoing &o) { return server.poll(nowMs, 0, false, nowMs / 1000, o); }
};

static void decodeOut(const Outgoing &o, BatchHeader &h, Item *items, uint8_t &n) {
    TEST_ASSERT_TRUE(decodeBatch(o.payload, o.len, h, items, 16, n));
}

void test_discover_yields_announce() {
    Rig rig(0, 5);
    rig.discover(0xC0FFEE, 2);
    Outgoing o;
    TEST_ASSERT_TRUE(rig.poll(0, o));
    TEST_ASSERT_EQUAL_HEX32(0xC0FFEE, o.to);
    TEST_ASSERT_EQUAL(-1, o.chanSlot);
    TEST_ASSERT_EQUAL(2, o.hopLimit);
    Announce a;
    TEST_ASSERT_TRUE(decodeAnnounce(o.payload, o.len, a));
    TEST_ASSERT_EQUAL_STRING("CSRV", a.shortName);
    TEST_ASSERT_EQUAL(2, a.count);
    TEST_ASSERT_EQUAL_HEX32(0x11111111, a.ch[0].id);
    TEST_ASSERT_EQUAL_STRING("camillia", a.ch[1].name);
    TEST_ASSERT_FALSE(rig.poll(10000, o));
}

void test_ignores_beyond_max_hops() {
    Rig rig(5, 5, /*maxHops=*/3);
    rig.request(0xA, 0, 4, 0);
    TEST_ASSERT_EQUAL(0, rig.server.queueLength());
    rig.discover(0xB, 4);
    Outgoing o;
    TEST_ASSERT_FALSE(rig.poll(0, o));
}

void test_ignores_request_on_discovery_or_unused_slot() {
    Rig rig(5, 5);
    Request r{0, 0, 0, 0};
    uint8_t buf[MAX_PAYLOAD];
    size_t n = encodeRequest(r, buf, sizeof buf);
    rig.server.onPacket(0xA, ME, 901, -1, 0, buf, n, 0);
    rig.server.onPacket(0xA, ME, 902, 2, 0, buf, n, 0);   // slot 2 exists but chanCount is 2
    TEST_ASSERT_EQUAL(0, rig.server.queueLength());
}

void test_reply_hop_limit_matches_request() {
    Rig rig(3, 5, /*maxHops=*/7);
    rig.request(0xA, 0, 2, 0);
    Outgoing o;
    TEST_ASSERT_TRUE(rig.poll(0, o));
    TEST_ASSERT_EQUAL(2, o.hopLimit);
    TEST_ASSERT_EQUAL(0, o.chanSlot);
    TEST_ASSERT_EQUAL_HEX32(0xA, o.to);
}

void test_request_paced_by_gap() {
    Rig rig(25, 150);   // 150-byte texts: one item per packet
    rig.request(0xA, 0, 0, 0);
    Outgoing o;
    TEST_ASSERT_TRUE(rig.poll(0, o));
    TEST_ASSERT_FALSE(rig.poll(2999, o));
    TEST_ASSERT_TRUE(rig.poll(3000, o));
    uint32_t t = 3000;
    BatchHeader h; Item items[16]; uint8_t n;
    int packets = 2;
    while (true) {
        decodeOut(o, h, items, n);
        if (h.flags & FLAG_LAST) break;
        t += 3000;
        TEST_ASSERT_TRUE(rig.poll(t, o));
        packets++;
    }
    TEST_ASSERT_EQUAL(10, packets);
    TEST_ASSERT_TRUE(h.flags & FLAG_MORE);
    TEST_ASSERT_EQUAL_UINT32(10, items[n - 1].seq);
    TEST_ASSERT_FALSE(rig.server.busy());
}

void test_queue_capacity_24() {
    Rig rig(5, 5);
    for (uint32_t i = 0; i < 25; i++) rig.request(0x100 + i, 0, 0, 0);
    TEST_ASSERT_EQUAL(24, rig.server.queueLength());
}

void test_queue_replaces_duplicate_requester() {
    Rig rig(20, 4);   // a 10-message batch fits one packet
    rig.request(0xB, 0, 0, 0);    // occupies the server first
    Outgoing o;
    TEST_ASSERT_TRUE(rig.poll(0, o));
    rig.request(0xA, 0, 0, 0);
    rig.request(0xA, 0, 0, 15);   // continuation replaces the queued request
    TEST_ASSERT_EQUAL(1, rig.server.queueLength());
    TEST_ASSERT_TRUE(rig.poll(3000, o));
    TEST_ASSERT_EQUAL_HEX32(0xA, o.to);
    BatchHeader h; Item items[16]; uint8_t n;
    decodeOut(o, h, items, n);
    TEST_ASSERT_EQUAL_UINT32(16, items[0].seq);
    TEST_ASSERT_EQUAL(5, n);
}

void test_serves_fifo_one_at_a_time() {
    Rig rig(25, 150);
    rig.request(0xA, 0, 0, 0);
    rig.request(0xB, 0, 0, 0);
    Outgoing o;
    uint32_t t = 0;
    int forA = 0;
    bool sawB = false;
    while (rig.poll(t, o) || rig.server.busy() || rig.server.queueLength()) {
        if (o.to == 0xA) { TEST_ASSERT_FALSE(sawB); forA++; }
        if (o.to == 0xB) sawB = true;
        t += 3000;
        if (t > 200000) break;
    }
    TEST_ASSERT_EQUAL(10, forA);
    TEST_ASSERT_TRUE(sawB);
}

void test_last_sent_at_tracks_batch_packets() {
    Rig rig(3, 5);
    uint32_t unix = 1, up = 1;
    TEST_ASSERT_FALSE(rig.server.lastSentAt(unix, up));
    rig.discover(0xB, 0);
    Outgoing o;
    TEST_ASSERT_TRUE(rig.server.poll(0, 1700000000, true, 50, o));   // announce only
    TEST_ASSERT_FALSE(rig.server.lastSentAt(unix, up));
    rig.request(0xA, 0, 0, 0);
    TEST_ASSERT_TRUE(rig.server.poll(5000, 1700000077, true, 77, o));
    TEST_ASSERT_TRUE(rig.server.lastSentAt(unix, up));
    TEST_ASSERT_EQUAL_UINT32(1700000077, unix);
    TEST_ASSERT_EQUAL_UINT32(77, up);
}

void test_clear_slot_drops_pending_and_active() {
    Rig rig(25, 150);
    rig.request(0xA, 0, 0, 0);
    Outgoing o;
    TEST_ASSERT_TRUE(rig.poll(0, o));          // transfer to A on slot 0 under way
    rig.request(0xB, 0, 0, 0);                 // queued, slot 0
    rig.request(0xC, 1, 0, 0);                 // queued, slot 1
    rig.server.clearSlot(0);
    TEST_ASSERT_FALSE(rig.server.busy());
    TEST_ASSERT_EQUAL(1, rig.server.queueLength());
    TEST_ASSERT_TRUE(rig.poll(3000, o));
    TEST_ASSERT_EQUAL_HEX32(0xC, o.to);
    TEST_ASSERT_EQUAL(1, o.chanSlot);
}

void test_ignores_request_addressed_to_other_node() {
    Rig rig(5, 5);
    rig.request(0xA, 0, 0, 0, 0, /*to=*/0x99999999);
    TEST_ASSERT_EQUAL(0, rig.server.queueLength());
}

void test_discover_broadcast_or_to_me_only() {
    Rig rig(0, 5);
    rig.discover(0xA, 0, /*to=*/0x99999999);
    Outgoing o;
    TEST_ASSERT_FALSE(rig.poll(0, o));
    rig.discover(0xA, 0, /*to=*/ME);
    TEST_ASSERT_TRUE(rig.poll(0, o));
}

void test_relayed_duplicate_request_ignored_after_dequeue() {
    Rig rig(3, 5);
    rig.request(0xA, 0, 0, 0, 0, ME, 777);
    Outgoing o;
    TEST_ASSERT_TRUE(rig.poll(0, o));              // served, LAST
    rig.request(0xA, 0, 1, 0, 1000, ME, 777);      // relay's copy of the same packet
    TEST_ASSERT_EQUAL(0, rig.server.queueLength());
    TEST_ASSERT_FALSE(rig.poll(5000, o));
}

void test_relayed_duplicate_discover_ignored() {
    Rig rig(0, 5);
    rig.discover(0xA, 0, 0xFFFFFFFF, 55);
    Outgoing o;
    TEST_ASSERT_TRUE(rig.poll(0, o));
    rig.discover(0xA, 1, 0xFFFFFFFF, 55);
    TEST_ASSERT_FALSE(rig.poll(5000, o));
}

void test_request_matching_active_transfer_ignored() {
    Rig rig(25, 150);
    rig.request(0xA, 0, 0, 0);
    Outgoing o;
    TEST_ASSERT_TRUE(rig.poll(0, o));              // transfer to A on slot 0 under way
    rig.request(0xA, 0, 0, 0);                     // new packet id, same node + slot
    TEST_ASSERT_EQUAL(0, rig.server.queueLength());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_since_uses_cursor_when_epoch_matches);
    RUN_TEST(test_since_uses_anchor_when_epoch_differs);
    RUN_TEST(test_since_everything_when_nothing_matches);
    RUN_TEST(test_since_cursor_beyond_head_falls_back);
    RUN_TEST(test_since_cursor_older_than_tail_sends_everything_held);
    RUN_TEST(test_plan_batch_sets_more);
    RUN_TEST(test_pack_respects_payload_limit);
    RUN_TEST(test_pack_short_messages_share_a_packet);
    RUN_TEST(test_pack_age_unknown_after_reboot);
    RUN_TEST(test_pack_empty_sends_one_last_packet);
    RUN_TEST(test_pack_never_exceeds_radio_limit);
    RUN_TEST(test_discover_yields_announce);
    RUN_TEST(test_ignores_beyond_max_hops);
    RUN_TEST(test_ignores_request_on_discovery_or_unused_slot);
    RUN_TEST(test_reply_hop_limit_matches_request);
    RUN_TEST(test_request_paced_by_gap);
    RUN_TEST(test_queue_capacity_24);
    RUN_TEST(test_queue_replaces_duplicate_requester);
    RUN_TEST(test_serves_fifo_one_at_a_time);
    RUN_TEST(test_last_sent_at_tracks_batch_packets);
    RUN_TEST(test_clear_slot_drops_pending_and_active);
    RUN_TEST(test_ignores_request_addressed_to_other_node);
    RUN_TEST(test_discover_broadcast_or_to_me_only);
    RUN_TEST(test_relayed_duplicate_request_ignored_after_dequeue);
    RUN_TEST(test_relayed_duplicate_discover_ignored);
    RUN_TEST(test_request_matching_active_transfer_ignored);
    return UNITY_END();
}
