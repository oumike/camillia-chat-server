#include <unity.h>
#include <string.h>
#include "messages_json.h"

void setUp() {}
void tearDown() {}

static StoredMsg msg(const char *text) {
    StoredMsg m{};
    m.seq = 42; m.from = 0xDEADBEEF; m.packetId = 7; m.rxUnix = 1700000000; m.rxUptimeSec = 0;
    m.source = SRC_MQTT;
    m.textLen = (uint8_t)strlen(text);
    memcpy(m.text, text, m.textLen);
    return m;
}

void test_message_json_fields() {
    char out[600];
    StoredMsg m = msg("hello");
    size_t n = messageJson(m, 125, out, sizeof out);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"seq\":42,\"from\":\"!deadbeef\",\"rxUnix\":1700000000,\"ageSec\":125,"
        "\"source\":\"mqtt\",\"text\":\"hello\"}", out);
}

void test_message_json_unknown_age_is_null() {
    char out[600];
    StoredMsg m = msg("x");
    m.source = SRC_LORA;
    messageJson(m, 0xFFFFFFFF, out, sizeof out);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"ageSec\":null"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"source\":\"lora\""));
}

void test_message_json_escapes_text() {
    char out[600];
    StoredMsg m = msg("say \"hi\"\\ \n\x01 caf\xC3\xA9 </script>");
    messageJson(m, 0, out, sizeof out);
    TEST_ASSERT_NOT_NULL(strstr(out,
        "\"text\":\"say \\\"hi\\\"\\\\ \\n\\u0001 caf\xC3\xA9 <\\/script>\""));
}

void test_message_json_too_small_buffer() {
    char out[20];
    StoredMsg m = msg("hello");
    TEST_ASSERT_EQUAL(0, messageJson(m, 0, out, sizeof out));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_message_json_fields);
    RUN_TEST(test_message_json_unknown_age_is_null);
    RUN_TEST(test_message_json_escapes_text);
    RUN_TEST(test_message_json_too_small_buffer);
    return UNITY_END();
}
