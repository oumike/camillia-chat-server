#include <unity.h>
#include <string.h>
#include "display_text.h"
#include "cs_proto.h"

void setUp() {}
void tearDown() {}

static void age(uint32_t s, const char *want) {
    char b[16]; formatAge(s, b, sizeof b); TEST_ASSERT_EQUAL_STRING(want, b);
}
static void up(uint32_t s, const char *want) {
    char b[16]; formatUptime(s, b, sizeof b); TEST_ASSERT_EQUAL_STRING(want, b);
}

void test_format_age() {
    age(0, "now"); age(59, "now"); age(60, "1m"); age(7200, "2h"); age(345600, "4d");
    age(csp::AGE_UNKNOWN, "?");
}
void test_format_uptime() {
    up(45, "45s"); up(720, "12m"); up(18180, "5h 3m"); up(273600, "3d 4h");
}
void test_one_line_replaces_newlines() {
    char o[32]; oneLine("a\nb\r\nc", o, sizeof o);
    TEST_ASSERT_EQUAL_STRING("a b  c", o);
    oneLine("x\ty", o, sizeof o);
    TEST_ASSERT_EQUAL_STRING("x y", o);
}
void test_one_line_cuts_with_ellipsis() {
    const char *in = "0123456789012345678901234567890123456789";
    char o[12]; oneLine(in, o, sizeof o);
    size_t n = strlen(o);
    TEST_ASSERT_TRUE(n <= 11);
    TEST_ASSERT_EQUAL_STRING("\xE2\x80\xA6", o + n - 3);
    TEST_ASSERT_EQUAL_MEMORY("01234567", o, 8);
}
void test_one_line_no_split_multibyte() {
    // each "é" is 2 bytes; cap 12 leaves 11 bytes: 3 for the ellipsis, 8 for text
    const char *in = "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9";
    char o[12]; oneLine(in, o, sizeof o);
    TEST_ASSERT_EQUAL_STRING("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xE2\x80\xA6", o);
    // odd budget: cap 11 leaves 10, 7 for text -> 3 whole chars only
    char p[11]; oneLine(in, p, sizeof p);
    TEST_ASSERT_EQUAL_STRING("\xC3\xA9\xC3\xA9\xC3\xA9\xE2\x80\xA6", p);
}
void test_one_line_fits() {
    char o[12]; oneLine("hello", o, sizeof o);
    TEST_ASSERT_EQUAL_STRING("hello", o);
    oneLine("01234567890", o, sizeof o);   // exactly 11 bytes: fits
    TEST_ASSERT_EQUAL_STRING("01234567890", o);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_format_age);
    RUN_TEST(test_format_uptime);
    RUN_TEST(test_one_line_replaces_newlines);
    RUN_TEST(test_one_line_cuts_with_ellipsis);
    RUN_TEST(test_one_line_no_split_multibyte);
    RUN_TEST(test_one_line_fits);
    return UNITY_END();
}
