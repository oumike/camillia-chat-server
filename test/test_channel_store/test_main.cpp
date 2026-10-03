#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "channel_store.h"

static uint32_t s_rand = 1000;
static uint32_t fakeRandom() { return ++s_rand; }

static ChannelStore *fresh() {
    ChannelStore *s = new ChannelStore();
    TEST_ASSERT_TRUE(s->begin(malloc, fakeRandom));
    return s;
}

static bool addMsg(ChannelStore &s, uint32_t from, uint32_t id, const char *text,
                   uint8_t source = 0) {
    return s.add(from, id, text, strlen(text), 1700000000 + id, 10 + id, source);
}

void setUp() {}
void tearDown() {}

void test_add_assigns_increasing_seq() {
    ChannelStore *s = fresh();
    TEST_ASSERT_TRUE(addMsg(*s, 1, 1, "a"));
    TEST_ASSERT_TRUE(addMsg(*s, 1, 2, "b"));
    TEST_ASSERT_TRUE(addMsg(*s, 2, 1, "c"));
    StoredMsg out[3];
    TEST_ASSERT_EQUAL(3, s->copyAfter(0, out, 3));
    TEST_ASSERT_EQUAL_UINT32(1, out[0].seq);
    TEST_ASSERT_EQUAL_UINT32(2, out[1].seq);
    TEST_ASSERT_EQUAL_UINT32(3, out[2].seq);
    TEST_ASSERT_EQUAL_UINT32(3, s->headSeq());
    TEST_ASSERT_EQUAL_UINT32(1, s->tailSeq());
    TEST_ASSERT_EQUAL(3, s->count());
    delete s;
}

void test_store_dedupes_relayed_and_mqtt_copies() {
    ChannelStore *s = fresh();
    TEST_ASSERT_TRUE(addMsg(*s, 0xA, 1, "hello", 0));
    TEST_ASSERT_FALSE(addMsg(*s, 0xA, 1, "hello", 1));
    TEST_ASSERT_EQUAL(1, s->count());
    delete s;
}

void test_ring_drops_oldest() {
    ChannelStore *s = fresh();
    for (uint32_t i = 1; i <= 260; i++) TEST_ASSERT_TRUE(addMsg(*s, 7, i, "m"));
    TEST_ASSERT_EQUAL(CS_MSGS_PER_CHANNEL, s->count());
    TEST_ASSERT_EQUAL_UINT32(11, s->tailSeq());
    TEST_ASSERT_EQUAL_UINT32(260, s->headSeq());
    // The evicted message (from 7, id 1) is no longer held, so it is accepted again.
    TEST_ASSERT_TRUE(addMsg(*s, 7, 1, "m"));
    delete s;
}

void test_store_truncates_on_utf8_boundary() {
    ChannelStore *s = fresh();
    char text[210];
    memset(text, 'x', 199);
    text[199] = (char)0xC3; text[200] = (char)0xA9;  // "é" straddles the 200-byte limit
    text[201] = 0;
    TEST_ASSERT_TRUE(s->add(1, 1, text, 201, 0, 1, 0));
    StoredMsg m;
    TEST_ASSERT_EQUAL(1, s->copyAfter(0, &m, 1));
    TEST_ASSERT_EQUAL(199, m.textLen);
    TEST_ASSERT_EQUAL_CHAR(0, m.text[199]);
    delete s;
}

void test_copy_after_and_anchor() {
    ChannelStore *s = fresh();
    for (uint32_t i = 1; i <= 5; i++) addMsg(*s, 9, 100 + i, "x");
    StoredMsg out[10];
    TEST_ASSERT_EQUAL(3, s->copyAfter(2, out, 10));
    TEST_ASSERT_EQUAL_UINT32(3, out[0].seq);
    TEST_ASSERT_EQUAL_UINT32(5, out[2].seq);
    TEST_ASSERT_EQUAL(2, s->copyAfter(2, out, 2));  // respects max
    TEST_ASSERT_EQUAL_UINT32(2, s->findSeqAfterAnchor(9, 102));
    TEST_ASSERT_EQUAL_UINT32(0, s->findSeqAfterAnchor(9, 999));
    delete s;
}

void test_uptime_zero_reserved_for_previous_boot() {
    ChannelStore *s = fresh();
    TEST_ASSERT_TRUE(s->add(1, 1, "x", 1, 0, 0, 0));  // received in the first second of uptime
    StoredMsg m;
    s->copyAfter(0, &m, 1);
    TEST_ASSERT_EQUAL_UINT32(1, m.rxUptimeSec);
    delete s;
}

void test_serialize_round_trip() {
    ChannelStore *s = fresh();
    for (uint32_t i = 1; i <= 30; i++) {
        char t[16]; snprintf(t, sizeof t, "msg %u", (unsigned)i);
        addMsg(*s, 3, i, t, i % 2);
    }
    std::vector<uint8_t> buf(ChannelStore::maxSerializedSize());
    size_t n = s->serialize(buf.data(), buf.size());
    TEST_ASSERT_GREATER_THAN(0, n);

    ChannelStore *t = fresh();
    TEST_ASSERT_TRUE(t->deserialize(buf.data(), n));
    TEST_ASSERT_EQUAL_UINT32(s->epoch(), t->epoch());
    TEST_ASSERT_EQUAL_UINT32(30, t->headSeq());
    TEST_ASSERT_EQUAL(30, t->count());
    StoredMsg m[30];
    TEST_ASSERT_EQUAL(30, t->copyAfter(0, m, 30));
    TEST_ASSERT_EQUAL_STRING("msg 17", m[16].text);
    TEST_ASSERT_EQUAL(1, m[16].source);
    TEST_ASSERT_EQUAL_UINT32(1700000017, m[16].rxUnix);
    TEST_ASSERT_EQUAL_UINT32(0, m[16].rxUptimeSec);   // loaded = previous boot
    TEST_ASSERT_FALSE(t->add(3, 5, "dup", 3, 0, 1, 0)); // dedupe survives a reload
    TEST_ASSERT_TRUE(t->add(3, 31, "new", 3, 0, 1, 0));
    TEST_ASSERT_EQUAL_UINT32(31, t->headSeq());
    delete s; delete t;
}

void test_store_rejects_corrupt_blob() {
    ChannelStore *s = fresh();
    for (uint32_t i = 1; i <= 5; i++) addMsg(*s, 3, i, "hello");
    std::vector<uint8_t> buf(ChannelStore::maxSerializedSize());
    size_t n = s->serialize(buf.data(), buf.size());

    ChannelStore *t = fresh();
    uint32_t epoch = t->epoch();
    buf[20] ^= 0xFF;
    TEST_ASSERT_FALSE(t->deserialize(buf.data(), n));
    TEST_ASSERT_FALSE(t->deserialize(buf.data(), n / 2));  // truncated
    TEST_ASSERT_FALSE(t->deserialize((const uint8_t *)"junk", 4));
    TEST_ASSERT_EQUAL(0, t->count());
    TEST_ASSERT_EQUAL_UINT32(epoch, t->epoch());
    delete s; delete t;
}

void test_dirty_tracking() {
    ChannelStore *s = fresh();
    TEST_ASSERT_FALSE(s->dirty());
    for (uint32_t i = 1; i <= 20; i++) addMsg(*s, 3, i, "x");
    TEST_ASSERT_TRUE(s->dirty());
    TEST_ASSERT_EQUAL_UINT32(20, s->addsSinceSave());
    s->markSaved();
    TEST_ASSERT_FALSE(s->dirty());
    TEST_ASSERT_EQUAL_UINT32(0, s->addsSinceSave());
    delete s;
}

void test_reset_clears_and_changes_epoch() {
    ChannelStore *s = fresh();
    addMsg(*s, 1, 1, "x");
    s->reset(4242);
    TEST_ASSERT_EQUAL(0, s->count());
    TEST_ASSERT_EQUAL_UINT32(4242, s->epoch());
    TEST_ASSERT_TRUE(s->dirty());
    TEST_ASSERT_TRUE(addMsg(*s, 1, 1, "x"));
    TEST_ASSERT_EQUAL_UINT32(1, s->headSeq());
    delete s;
}

void test_text_bytes_and_ram() {
    ChannelStore *s = fresh();
    TEST_ASSERT_EQUAL(0, s->textBytes());
    addMsg(*s, 1, 1, "hello");
    addMsg(*s, 1, 2, "hi");
    TEST_ASSERT_EQUAL(7, s->textBytes());
    TEST_ASSERT_EQUAL(sizeof(StoredMsg) * CS_MSGS_PER_CHANNEL, ChannelStore::ramBytes());
    s->reset(9);
    TEST_ASSERT_EQUAL(0, s->textBytes());
    delete s;
}

void test_reload_keeps_sequence_contiguous() {
    ChannelStore *s = fresh();
    for (uint32_t i = 1; i <= 30; i++) addMsg(*s, 3, i, "x");
    std::vector<uint8_t> buf(ChannelStore::maxSerializedSize());
    size_t n = s->serialize(buf.data(), buf.size());
    ChannelStore *t = fresh();
    TEST_ASSERT_TRUE(t->deserialize(buf.data(), n));
    TEST_ASSERT_EQUAL_UINT32(30, t->headSeq());
    TEST_ASSERT_TRUE(t->add(9, 1, "after reboot", 12, 0, 1, 0));
    TEST_ASSERT_EQUAL_UINT32(31, t->headSeq());
    delete s; delete t;
}

void test_newest_across() {
    ChannelStore st[2];
    TEST_ASSERT_TRUE(st[0].begin(malloc, fakeRandom));
    TEST_ASSERT_TRUE(st[1].begin(malloc, fakeRandom));
    // previous-boot message (as after deserialize): rxUptimeSec 0
    std::vector<uint8_t> buf(ChannelStore::maxSerializedSize());
    ChannelStore *old = fresh();
    old->add(7, 1, "old", 3, 1600000000, 5, 0);
    size_t n = old->serialize(buf.data(), buf.size());
    TEST_ASSERT_TRUE(st[1].deserialize(buf.data(), n));
    delete old;
    st[0].add(1, 1, "a1", 2, 1700000001, 100, 0);
    st[1].add(2, 1, "b1", 2, 1700000002, 200, 0);
    st[0].add(1, 2, "a2", 2, 1700000003, 300, 0);
    st[1].add(2, 2, "b2", 2, 1700000004, 400, 0);
    st[0].add(1, 3, "a3", 2, 1700000005, 500, 0);
    StoredMsg out[10]; int8_t slots[10];
    TEST_ASSERT_EQUAL(6, newestAcross(st, 2, out, slots, 10));
    const char *want[] = {"a3", "b2", "a2", "b1", "a1", "old"};
    int8_t wslot[] = {0, 1, 0, 1, 0, 1};
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_STRING(want[i], out[i].text);
        TEST_ASSERT_EQUAL(wslot[i], slots[i]);
    }
    TEST_ASSERT_EQUAL(3, newestAcross(st, 2, out, nullptr, 3));
    TEST_ASSERT_EQUAL_STRING("b2", out[1].text);
}

void test_newest_across_skips_empty_and_orders_old() {
    ChannelStore st[3];
    for (auto &s : st) TEST_ASSERT_TRUE(s.begin(malloc, fakeRandom));
    std::vector<uint8_t> buf(ChannelStore::maxSerializedSize());
    ChannelStore *o1 = fresh(), *o2 = fresh();
    o1->add(1, 1, "u1", 2, 1000, 1, 0);
    o2->add(1, 1, "u2", 2, 2000, 1, 0);
    size_t n = o1->serialize(buf.data(), buf.size());
    TEST_ASSERT_TRUE(st[0].deserialize(buf.data(), n));
    n = o2->serialize(buf.data(), buf.size());
    TEST_ASSERT_TRUE(st[2].deserialize(buf.data(), n));
    delete o1; delete o2;
    StoredMsg out[4]; int8_t slots[4];
    TEST_ASSERT_EQUAL(2, newestAcross(st, 3, out, slots, 4));
    TEST_ASSERT_EQUAL_STRING("u2", out[0].text);
    TEST_ASSERT_EQUAL(2, slots[0]);
    TEST_ASSERT_EQUAL(0, slots[1]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_add_assigns_increasing_seq);
    RUN_TEST(test_store_dedupes_relayed_and_mqtt_copies);
    RUN_TEST(test_ring_drops_oldest);
    RUN_TEST(test_store_truncates_on_utf8_boundary);
    RUN_TEST(test_copy_after_and_anchor);
    RUN_TEST(test_uptime_zero_reserved_for_previous_boot);
    RUN_TEST(test_serialize_round_trip);
    RUN_TEST(test_store_rejects_corrupt_blob);
    RUN_TEST(test_dirty_tracking);
    RUN_TEST(test_reset_clears_and_changes_epoch);
    RUN_TEST(test_text_bytes_and_ram);
    RUN_TEST(test_reload_keeps_sequence_contiguous);
    RUN_TEST(test_newest_across);
    RUN_TEST(test_newest_across_skips_empty_and_orders_old);
    return UNITY_END();
}
