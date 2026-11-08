#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "node_names.h"

void setUp() {}
void tearDown() {}

static NodeNames *fresh() {
    NodeNames *n = new NodeNames();
    TEST_ASSERT_TRUE(n->begin(malloc));
    return n;
}

static void name(const NodeNames &n, uint32_t id, char *out, size_t cap = 32) {
    n.displayName(id, out, cap);
}

void test_fallback_order() {
    NodeNames *n = fresh();
    char out[32];
    name(*n, 0xabcd, out);
    TEST_ASSERT_EQUAL_STRING("!0000abcd", out);
    n->update(0xabcd, "", "AB", 1);
    name(*n, 0xabcd, out);
    TEST_ASSERT_EQUAL_STRING("AB", out);
    n->update(0xabcd, "Alice", "", 2);
    name(*n, 0xabcd, out);
    TEST_ASSERT_EQUAL_STRING("Alice", out);
    n->update(0xabcd, "", "", 3);  // empty long name must not wipe Alice
    name(*n, 0xabcd, out);
    TEST_ASSERT_EQUAL_STRING("Alice", out);
    TEST_ASSERT_EQUAL(1, n->count());
    delete n;
}

void test_evicts_least_recent() {
    NodeNames *n = fresh();
    char out[32];
    for (uint32_t i = 1; i <= NODE_NAMES_CAP; i++) n->update(i, "N", "", i);
    TEST_ASSERT_EQUAL(NODE_NAMES_CAP, n->count());
    n->update(1, "N", "", 300);
    n->update(1000, "New", "", 301);
    TEST_ASSERT_EQUAL(NODE_NAMES_CAP, n->count());
    name(*n, 2, out);
    TEST_ASSERT_EQUAL_STRING("!00000002", out);
    name(*n, 1, out);
    TEST_ASSERT_EQUAL_STRING("N", out);
    name(*n, 1000, out);
    TEST_ASSERT_EQUAL_STRING("New", out);
    delete n;
}

void test_long_name_cut_on_utf8_boundary() {
    NodeNames *n = fresh();
    char in[64] = {0};
    for (int i = 0; i < 15; i++) strcat(in, "\xc3\xa9");  // 30 bytes
    n->update(5, in, "", 1);
    char out[64];
    name(*n, 5, out, sizeof(out));
    TEST_ASSERT_EQUAL(24, (int)strlen(out));
    TEST_ASSERT_EQUAL_MEMORY(in, out, 24);
    // Odd cut: 3-byte chars, 24 is a boundary; use 'a' + 2-byte chars so the cut lands mid-char.
    char in2[64] = "a";
    for (int i = 0; i < 14; i++) strcat(in2, "\xc3\xa9");  // 29 bytes; byte 24 is a lead byte at offset 23
    n->update(6, in2, "", 2);
    name(*n, 6, out, sizeof(out));
    TEST_ASSERT_EQUAL(23, (int)strlen(out));
    delete n;
}

void test_short_name_cut_to_4_bytes() {
    NodeNames *n = fresh();
    char out[32];
    n->update(9, "", "ABCDEFG", 1);
    name(*n, 9, out);
    TEST_ASSERT_EQUAL_STRING("ABCD", out);
    delete n;
}

void test_null_names_ok() {
    NodeNames *n = fresh();
    char out[32];
    n->update(0x10, nullptr, nullptr, 1);
    name(*n, 0x10, out);
    TEST_ASSERT_EQUAL_STRING("!00000010", out);
    TEST_ASSERT_EQUAL(0, n->count());
    delete n;
}

void test_small_cap_terminates() {
    NodeNames *n = fresh();
    char out[6];
    memset(out, 'x', sizeof(out));
    n->displayName(0xabcd, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("!0000", out);
    n->displayName(1, out, 0);  // must not write
    delete n;
}

void test_name_parts() {
    NodeNames *n = fresh();
    TEST_ASSERT_EQUAL(0, n->nameParts(0x10));
    n->update(0x10, "", "AB", 1);
    TEST_ASSERT_EQUAL(NodeNames::SHORT_NAME, n->nameParts(0x10));
    n->update(0x10, "Alice", "", 2);
    TEST_ASSERT_EQUAL(NodeNames::LONG_NAME | NodeNames::SHORT_NAME, n->nameParts(0x10));
    n->update(0x11, "Bob", nullptr, 3);
    TEST_ASSERT_EQUAL(NodeNames::LONG_NAME, n->nameParts(0x11));
    delete n;
}

void test_revision_counts_changes() {
    NodeNames *n = fresh();
    uint32_t r0 = n->revision();
    n->update(0x10, "", "", 1);   // creates nothing
    TEST_ASSERT_EQUAL(r0, n->revision());
    n->update(0x10, "Alice", "AL", 2);
    TEST_ASSERT_NOT_EQUAL(r0, n->revision());
    delete n;
}

void test_blob_round_trip_keeps_names_and_eviction_order() {
    NodeNames *a = fresh();
    for (uint32_t i = 0; i < NODE_NAMES_CAP; i++) {
        char ln[16];
        snprintf(ln, sizeof ln, "Node %u", (unsigned)i);
        a->update(0x1000 + i, ln, "N", 1000 + i);   // 0x1000 is least recent
    }
    a->update(0x1000, nullptr, "Z", 5000);          // now 0x1001 is least recent
    static uint8_t blob[NodeNames::BLOB_MAX];
    size_t len = a->toBlob(blob, sizeof blob, 6000);
    TEST_ASSERT_TRUE(len > 0 && len <= sizeof blob);

    NodeNames *b = fresh();
    TEST_ASSERT_TRUE(b->fromBlob(blob, len, 10));   // after a reboot millis() is small
    TEST_ASSERT_EQUAL(NODE_NAMES_CAP, b->count());
    char out[32];
    b->displayName(0x1005, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("Node 5", out);
    TEST_ASSERT_EQUAL(NodeNames::LONG_NAME | NodeNames::SHORT_NAME, b->nameParts(0x1000));
    b->update(0x9999, "New", "NEW", 20);   // evicts 0x1001, the least recent
    TEST_ASSERT_EQUAL(0, b->nameParts(0x1001));
    TEST_ASSERT_NOT_EQUAL(0, b->nameParts(0x1000));
    delete a;
    delete b;
}

void test_blob_rejects_corrupt() {
    NodeNames *a = fresh();
    a->update(0x10, "Alice", "AL", 1);
    uint8_t blob[NodeNames::BLOB_MAX];
    size_t len = a->toBlob(blob, sizeof blob, 2);
    NodeNames *b = fresh();
    b->update(0x20, "Keep", "K", 1);
    blob[len - 3] ^= 0x40;
    TEST_ASSERT_FALSE(b->fromBlob(blob, len, 5));
    TEST_ASSERT_FALSE(b->fromBlob(blob, len - 1, 5));
    TEST_ASSERT_FALSE(b->fromBlob(blob, 3, 5));
    TEST_ASSERT_EQUAL(1, b->count());   // unchanged on failure
    TEST_ASSERT_EQUAL(0, a->toBlob(blob, 8, 2));   // too small
    delete a;
    delete b;
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_name_parts);
    RUN_TEST(test_revision_counts_changes);
    RUN_TEST(test_blob_round_trip_keeps_names_and_eviction_order);
    RUN_TEST(test_blob_rejects_corrupt);
    RUN_TEST(test_fallback_order);
    RUN_TEST(test_evicts_least_recent);
    RUN_TEST(test_long_name_cut_on_utf8_boundary);
    RUN_TEST(test_short_name_cut_to_4_bytes);
    RUN_TEST(test_null_names_ok);
    RUN_TEST(test_small_cap_terminates);
    return UNITY_END();
}
