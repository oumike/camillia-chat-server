#include <unity.h>
#include <string.h>
#include "cs_proto.h"

using namespace csp;

void setUp() {}
void tearDown() {}

static Item makeItem(uint32_t seq, const char *text) {
    Item it{};
    it.seq = seq; it.from = 0x1000 + seq; it.packetId = 0x2000 + seq; it.ageSec = 60 * seq;
    it.textLen = (uint8_t)strlen(text);
    memcpy(it.text, text, it.textLen);
    return it;
}

void test_round_trip_each_type() {
    uint8_t buf[MAX_PAYLOAD];

    Announce a{};
    strcpy(a.shortName, "CSRV");
    a.count = 2;
    a.ch[0].id = 0x11223344; strcpy(a.ch[0].name, "LongFast");
    a.ch[1].id = 0x55667788; strcpy(a.ch[1].name, "camillia");
    size_t n = encodeAnnounce(a, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    Announce a2{};
    TEST_ASSERT_TRUE(decodeAnnounce(buf, n, a2));
    TEST_ASSERT_EQUAL_STRING("CSRV", a2.shortName);
    TEST_ASSERT_EQUAL(2, a2.count);
    TEST_ASSERT_EQUAL_HEX32(0x11223344, a2.ch[0].id);
    TEST_ASSERT_EQUAL_STRING("LongFast", a2.ch[0].name);
    TEST_ASSERT_EQUAL_HEX32(0x55667788, a2.ch[1].id);
    TEST_ASSERT_EQUAL_STRING("camillia", a2.ch[1].name);

    Request r{7, 42, 0xA1B2C3D4, 99};
    n = encodeRequest(r, buf, sizeof(buf));
    Request r2{};
    TEST_ASSERT_TRUE(decodeRequest(buf, n, r2));
    TEST_ASSERT_EQUAL_UINT32(7, r2.epoch);
    TEST_ASSERT_EQUAL_UINT32(42, r2.cursor);
    TEST_ASSERT_EQUAL_HEX32(0xA1B2C3D4, r2.anchorFrom);
    TEST_ASSERT_EQUAL_UINT32(99, r2.anchorId);

    Item items[3] = { makeItem(1, "hi"), makeItem(2, "hello there"), makeItem(3, "") };
    BatchHeader h{0xDEADBEEF, FLAG_LAST | FLAG_MORE, 1700000123, 3};
    n = encodeBatch(h, items, 3, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    BatchHeader h2{};
    Item out[5];
    uint8_t got = 0;
    TEST_ASSERT_TRUE(decodeBatch(buf, n, h2, out, 5, got));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, h2.epoch);
    TEST_ASSERT_EQUAL(FLAG_LAST | FLAG_MORE, h2.flags);
    TEST_ASSERT_EQUAL_UINT32(1700000123, h2.serverTime);
    TEST_ASSERT_EQUAL(3, got);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_UINT32(items[i].seq, out[i].seq);
        TEST_ASSERT_EQUAL_UINT32(items[i].from, out[i].from);
        TEST_ASSERT_EQUAL_UINT32(items[i].packetId, out[i].packetId);
        TEST_ASSERT_EQUAL_UINT32(items[i].ageSec, out[i].ageSec);
        TEST_ASSERT_EQUAL(items[i].textLen, out[i].textLen);
        if (items[i].textLen) TEST_ASSERT_EQUAL_MEMORY(items[i].text, out[i].text, items[i].textLen);
        TEST_ASSERT_EQUAL_CHAR(0, out[i].text[out[i].textLen]);
    }
}

void test_header_bytes() {
    uint8_t buf[MAX_PAYLOAD];
    TEST_ASSERT_EQUAL(2, encodeDiscover(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[1]);

    Request r{0x04030201, 0, 0, 0};
    TEST_ASSERT_EQUAL(18, encodeRequest(r, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x04, buf[5]);

    Type t;
    TEST_ASSERT_TRUE(peekType(buf, 18, t));
    TEST_ASSERT_EQUAL(REQUEST, t);
}

void test_max_item_fits() {
    char text[201];
    memset(text, 'x', 200); text[200] = 0;
    Item it = makeItem(1, text);
    TEST_ASSERT_EQUAL(217, itemWireSize(it));
    BatchHeader h{1, FLAG_LAST, 0, 1};
    uint8_t buf[MAX_PAYLOAD];
    size_t n = encodeBatch(h, &it, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(229, n);
    TEST_ASSERT_LESS_OR_EQUAL(MAX_PAYLOAD, n);
}

void test_encoders_reject_small_buffer() {
    uint8_t buf[10];
    Request r{1, 2, 3, 4};
    TEST_ASSERT_EQUAL(0, encodeRequest(r, buf, sizeof(buf)));
}

void test_decode_rejects() {
    uint8_t buf[MAX_PAYLOAD];
    Request r{1, 2, 3, 4};
    size_t n = encodeRequest(r, buf, sizeof(buf));
    Request r2;

    buf[0] = 2;  // unknown version
    TEST_ASSERT_FALSE(decodeRequest(buf, n, r2));
    buf[0] = VERSION;
    TEST_ASSERT_FALSE(decodeRequest(buf, 17, r2));  // truncated
    Announce a;
    TEST_ASSERT_FALSE(decodeAnnounce(buf, n, a));   // wrong type

    Item items[2] = { makeItem(1, "a"), makeItem(2, "b") };
    BatchHeader h{1, FLAG_LAST, 0, 2};
    n = encodeBatch(h, items, 2, buf, sizeof(buf));
    BatchHeader h2; Item out[4]; uint8_t got;
    buf[11] = 3;  // count says 3, only 2 present
    TEST_ASSERT_FALSE(decodeBatch(buf, n, h2, out, 4, got));
    buf[11] = 2;
    TEST_ASSERT_TRUE(decodeBatch(buf, n, h2, out, 4, got));
    buf[12 + 16] = 201;  // first item's textLen beyond the 200-byte limit
    TEST_ASSERT_FALSE(decodeBatch(buf, n, h2, out, 4, got));
}

void test_channel_id_stable() {
    const uint8_t psk1[1] = {0x01};
    const uint8_t psk2[1] = {0x02};
    // sha256("LongFast" || expanded default key)[0..3], little-endian
    TEST_ASSERT_EQUAL_HEX32(0xf989c0d1, channelId("LongFast", psk1, 1));
    TEST_ASSERT_EQUAL_HEX32(0x70983e62, channelId("LongFast", psk2, 1));
    // A 1-byte PSK and its expanded 16-byte form are the same channel.
    const uint8_t full[16] = {0xd4,0xf1,0xbb,0x3a,0x20,0x29,0x07,0x59,
                              0xf0,0xbc,0xff,0xab,0xcf,0x4e,0x69,0x01};
    TEST_ASSERT_EQUAL_HEX32(channelId("LongFast", psk1, 1), channelId("LongFast", full, 16));
    TEST_ASSERT_NOT_EQUAL(channelId("LongFast", psk1, 1), channelId("Longfast", psk1, 1));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_each_type);
    RUN_TEST(test_header_bytes);
    RUN_TEST(test_max_item_fits);
    RUN_TEST(test_encoders_reject_small_buffer);
    RUN_TEST(test_decode_rejects);
    RUN_TEST(test_channel_id_stable);
    return UNITY_END();
}
