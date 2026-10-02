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
    ChannelStore *s = storeWith(10, 5);
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
    return UNITY_END();
}
