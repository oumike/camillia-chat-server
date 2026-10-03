#include <unity.h>
#include "airtime.h"

void setUp() {}
void tearDown() {}

void test_time_on_air_longfast() {
    // SF11 / 250 kHz / CR 4/5, 16-symbol preamble, explicit header, CRC on.
    TEST_ASSERT_UINT32_WITHIN(2, 641, timeOnAirMs(50, 11, 250.0f, 5, 16));
    TEST_ASSERT_UINT32_WITHIN(2, 2034, timeOnAirMs(237, 11, 250.0f, 5, 16));
}

void test_time_on_air_low_data_rate_optimize() {
    // SF12 / 125 kHz: symbol time 32.8 ms, so LDRO is on. 20 bytes ≈ 1.58 s.
    TEST_ASSERT_UINT32_WITHIN(2, 1581, timeOnAirMs(20, 12, 125.0f, 5, 16));
}

void test_budget_allows_under_limit() {
    AirtimeBudget b;
    // 10% of an hour = 360 000 ms.
    TEST_ASSERT_TRUE(b.canSend(0, 1000, 10));
    b.record(0, 359000);
    TEST_ASSERT_TRUE(b.canSend(1, 1000, 10));
    b.record(1, 1000);
    TEST_ASSERT_FALSE(b.canSend(2, 1, 10));
}

void test_budget_frees_after_window() {
    AirtimeBudget b;
    b.record(1000, 360000);
    TEST_ASSERT_FALSE(b.canSend(5000, 1000, 10));
    TEST_ASSERT_TRUE(b.canSend(1000 + 3600000, 1000, 10));
}

void test_budget_unlimited_at_100() {
    AirtimeBudget b;
    b.record(0, 3600000);
    TEST_ASSERT_TRUE(b.canSend(1, 5000, 100));
}

void test_budget_survives_millis_wrap() {
    AirtimeBudget b;
    b.record(0xFFFFF000u, 360000);
    TEST_ASSERT_FALSE(b.canSend(0x00000100u, 1000, 10));
    TEST_ASSERT_TRUE(b.canSend(0xFFFFF000u + 3600000u, 1000, 10));
}

void test_budget_full_log_never_undercounts() {
    AirtimeBudget b;
    for (uint32_t i = 0; i < 2000; i++) b.record(i, 200);   // 400 s of airtime, more entries than the log holds
    TEST_ASSERT_FALSE(b.canSend(2000, 1, 10));               // 400 s > 360 s: still refused
}

void test_used_ms() {
    AirtimeBudget b;
    b.record(0, 1000);
    b.record(1800000, 500);
    TEST_ASSERT_EQUAL_UINT32(1500, b.usedMs(1900000));
    TEST_ASSERT_EQUAL_UINT32(500, b.usedMs(3600001));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_time_on_air_longfast);
    RUN_TEST(test_time_on_air_low_data_rate_optimize);
    RUN_TEST(test_budget_allows_under_limit);
    RUN_TEST(test_budget_frees_after_window);
    RUN_TEST(test_budget_unlimited_at_100);
    RUN_TEST(test_budget_survives_millis_wrap);
    RUN_TEST(test_budget_full_log_never_undercounts);
    RUN_TEST(test_used_ms);
    return UNITY_END();
}
