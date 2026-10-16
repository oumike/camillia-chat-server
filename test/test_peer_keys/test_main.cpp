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

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_find_and_replace);
    RUN_TEST(test_zero_key_ignored);
    RUN_TEST(test_evicts_least_recent);
    return UNITY_END();
}
