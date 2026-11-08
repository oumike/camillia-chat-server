#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include "node_names.h"
#include "peer_keys.h"
#include "sender_filter.h"
#include "settings.h"

void setUp() {}
void tearDown() {}

static NodeNames names;
static PeerKeys keys;

// 1: short name only. 2: long + short. 3: long + short + key. 4: key, no name.
// 5: long name + key. 6: never heard.
static void fill() {
    TEST_ASSERT_TRUE(names.begin(malloc));
    TEST_ASSERT_TRUE(keys.begin(malloc));
    uint8_t k[32];
    memset(k, 0x5A, sizeof k);
    names.update(1, "", "ONE", 1);
    names.update(2, "Two", "TWO", 1);
    names.update(3, "Three", "THR", 1);
    keys.update(3, k, 1);
    keys.update(4, k, 1);
    names.update(5, "Five", "", 1);
    keys.update(5, k, 1);
}

static bool ok(uint8_t level, uint32_t id) { return senderAllowed(level, names, keys, id); }

void test_off_allows_everyone() {
    for (uint32_t id = 1; id <= 6; id++) TEST_ASSERT_TRUE(ok(SENDER_FILTER_OFF, id));
}

void test_name_needs_any_name() {
    TEST_ASSERT_TRUE(ok(SENDER_FILTER_NAME, 1));
    TEST_ASSERT_TRUE(ok(SENDER_FILTER_NAME, 2));
    TEST_ASSERT_TRUE(ok(SENDER_FILTER_NAME, 5));
    TEST_ASSERT_FALSE(ok(SENDER_FILTER_NAME, 4));
    TEST_ASSERT_FALSE(ok(SENDER_FILTER_NAME, 6));
}

void test_full_name_needs_both_names() {
    TEST_ASSERT_FALSE(ok(SENDER_FILTER_FULL_NAME, 1));
    TEST_ASSERT_TRUE(ok(SENDER_FILTER_FULL_NAME, 2));
    TEST_ASSERT_TRUE(ok(SENDER_FILTER_FULL_NAME, 3));
    TEST_ASSERT_FALSE(ok(SENDER_FILTER_FULL_NAME, 5));
    TEST_ASSERT_FALSE(ok(SENDER_FILTER_FULL_NAME, 6));
}

void test_name_key_needs_name_and_key() {
    TEST_ASSERT_FALSE(ok(SENDER_FILTER_NAME_KEY, 2));
    TEST_ASSERT_TRUE(ok(SENDER_FILTER_NAME_KEY, 3));
    TEST_ASSERT_FALSE(ok(SENDER_FILTER_NAME_KEY, 4));
    TEST_ASSERT_TRUE(ok(SENDER_FILTER_NAME_KEY, 5));
    TEST_ASSERT_FALSE(ok(SENDER_FILTER_NAME_KEY, 6));
}

void test_unknown_level_blocks() {
    TEST_ASSERT_FALSE(ok(SENDER_FILTER_NAME_KEY + 1, 3));
}

int main() {
    UNITY_BEGIN();
    fill();
    RUN_TEST(test_off_allows_everyone);
    RUN_TEST(test_name_needs_any_name);
    RUN_TEST(test_full_name_needs_both_names);
    RUN_TEST(test_name_key_needs_name_and_key);
    RUN_TEST(test_unknown_level_blocks);
    return UNITY_END();
}
