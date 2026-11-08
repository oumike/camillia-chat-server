#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include "peer_keys.h"

void setUp() {}
void tearDown() {}

static PeerKeys *fresh() {
    PeerKeys *k = new PeerKeys();
    TEST_ASSERT_TRUE(k->begin(malloc));
    return k;
}

static void keyOf(uint8_t out[32], uint8_t fill) { memset(out, fill, 32); }

void test_find_and_replace() {
    PeerKeys *k = fresh();
    uint8_t a[32], b[32];
    keyOf(a, 0x11);
    keyOf(b, 0x22);
    TEST_ASSERT_NULL(k->find(0xabcd));
    k->update(0xabcd, a, 1);
    TEST_ASSERT_EQUAL_MEMORY(a, k->find(0xabcd), 32);
    k->update(0xabcd, b, 2);   // a node that regenerated its key
    TEST_ASSERT_EQUAL_MEMORY(b, k->find(0xabcd), 32);
    TEST_ASSERT_EQUAL(1, k->count());
    delete k;
}

void test_zero_key_ignored() {
    PeerKeys *k = fresh();
    uint8_t a[32], zero[32];
    keyOf(a, 0x11);
    keyOf(zero, 0);
    k->update(1, zero, 1);
    TEST_ASSERT_NULL(k->find(1));
    k->update(1, a, 2);
    k->update(1, zero, 3);     // NODEINFO without a key keeps the one we have
    TEST_ASSERT_EQUAL_MEMORY(a, k->find(1), 32);
    delete k;
}

void test_evicts_least_recent() {
    PeerKeys *k = fresh();
    uint8_t a[32];
    keyOf(a, 0x33);
    for (int i = 0; i < PEER_KEYS_CAP; i++) k->update(100 + i, a, 10 + i);
    k->update(100, a, 1000);   // refresh the oldest, so 101 is now oldest
    k->update(999, a, 1001);
    TEST_ASSERT_EQUAL(PEER_KEYS_CAP, k->count());
    TEST_ASSERT_NOT_NULL(k->find(100));
    TEST_ASSERT_NULL(k->find(101));
    TEST_ASSERT_NOT_NULL(k->find(999));
    delete k;
}

void test_revision_counts_changes() {
    PeerKeys *k = fresh();
    uint8_t a[32], z[32];
    keyOf(a, 0x11);
    keyOf(z, 0);
    uint32_t r0 = k->revision();
    k->update(1, z, 1);
    TEST_ASSERT_EQUAL(r0, k->revision());
    k->update(1, a, 2);
    TEST_ASSERT_NOT_EQUAL(r0, k->revision());
    delete k;
}

void test_blob_round_trip_keeps_keys_and_eviction_order() {
    PeerKeys *a = fresh();
    uint8_t key[32];
    for (uint32_t i = 0; i < PEER_KEYS_CAP; i++) {
        keyOf(key, (uint8_t)(i + 1));
        a->update(0x1000 + i, key, 1000 + i);
    }
    keyOf(key, 0xAA);
    a->update(0x1000, key, 5000);   // 0x1001 now least recent
    static uint8_t blob[PeerKeys::BLOB_MAX];
    size_t len = a->toBlob(blob, sizeof blob, 6000);
    TEST_ASSERT_TRUE(len > 0 && len <= sizeof blob);

    PeerKeys *b = fresh();
    TEST_ASSERT_TRUE(b->fromBlob(blob, len, 10));
    TEST_ASSERT_EQUAL(PEER_KEYS_CAP, b->count());
    keyOf(key, 6);
    TEST_ASSERT_EQUAL_MEMORY(key, b->find(0x1005), 32);
    keyOf(key, 0xBB);
    b->update(0x9999, key, 20);
    TEST_ASSERT_NULL(b->find(0x1001));
    TEST_ASSERT_NOT_NULL(b->find(0x1000));
    delete a;
    delete b;
}

void test_blob_rejects_corrupt() {
    PeerKeys *a = fresh();
    uint8_t key[32];
    keyOf(key, 0x11);
    a->update(0x10, key, 1);
    uint8_t blob[PeerKeys::BLOB_MAX];
    size_t len = a->toBlob(blob, sizeof blob, 2);
    PeerKeys *b = fresh();
    blob[len - 3] ^= 0x40;
    TEST_ASSERT_FALSE(b->fromBlob(blob, len, 5));
    TEST_ASSERT_FALSE(b->fromBlob(blob, 3, 5));
    TEST_ASSERT_EQUAL(0, b->count());
    delete a;
    delete b;
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_revision_counts_changes);
    RUN_TEST(test_blob_round_trip_keeps_keys_and_eviction_order);
    RUN_TEST(test_blob_rejects_corrupt);
    RUN_TEST(test_find_and_replace);
    RUN_TEST(test_zero_key_ignored);
    RUN_TEST(test_evicts_least_recent);
    return UNITY_END();
}
