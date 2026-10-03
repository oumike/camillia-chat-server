#include <unity.h>
#include "page_cycler.h"

void setUp() {}
void tearDown() {}

static const CyclerConfig CFG = {30, 120, 160, 0};

void test_rotates() {
    PageCycler c; c.begin(CFG, 0);
    c.tick(0, true);      TEST_ASSERT_EQUAL(0, c.page());
    c.tick(29900, true);  TEST_ASSERT_EQUAL(0, c.page());
    c.tick(30000, true);  TEST_ASSERT_EQUAL(1, c.page());
    c.tick(60000, true);  TEST_ASSERT_EQUAL(2, c.page());
    c.tick(90000, true);  TEST_ASSERT_EQUAL(0, c.page());
}

void test_tap_advances_and_restarts() {
    PageCycler c; c.begin(CFG, 0);
    c.tick(10000, true); c.tap(10000);
    TEST_ASSERT_EQUAL(1, c.page());
    c.tick(39000, true); TEST_ASSERT_EQUAL(1, c.page());
    c.tick(40000, true); TEST_ASSERT_EQUAL(2, c.page());
}

void test_battery_dims() {
    PageCycler c; c.begin(CFG, 0);
    c.tick(119000, false); TEST_ASSERT_EQUAL(160, c.backlight());
    c.tick(120000, false); TEST_ASSERT_EQUAL(0, c.backlight());
}

void test_tap_while_dim_only_wakes() {
    PageCycler c; c.begin(CFG, 0);
    // keep the page timer from firing between the last sync and the tap
    for (uint32_t t = 0; t <= 130000; t += 10000) c.tick(t, false);
    TEST_ASSERT_EQUAL(0, c.backlight());
    int before = c.page();
    c.tap(130000);
    TEST_ASSERT_EQUAL(160, c.backlight());
    TEST_ASSERT_EQUAL(before, c.page());
    c.tick(159000, false); TEST_ASSERT_EQUAL(before, c.page());
    c.tick(160000, false); TEST_ASSERT_EQUAL((before + 1) % 3, c.page());
}

void test_external_never_dims() {
    PageCycler c; c.begin(CFG, 0);
    c.tick(10000000, true);
    TEST_ASSERT_EQUAL(160, c.backlight());
}

void test_power_loss_starts_timeout() {
    PageCycler c; c.begin(CFG, 0);
    for (uint32_t t = 0; t < 500000; t += 1000) c.tick(t, true);
    c.tick(500000, false);
    c.tick(619000, false); TEST_ASSERT_EQUAL(160, c.backlight());
    c.tick(620000, false); TEST_ASSERT_EQUAL(0, c.backlight());
}

void test_zero_never_dims() {
    CyclerConfig cfg = CFG; cfg.dimAfterSec = 0;
    PageCycler c; c.begin(cfg, 0);
    c.tick(10000000, false);
    TEST_ASSERT_EQUAL(160, c.backlight());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_rotates);
    RUN_TEST(test_tap_advances_and_restarts);
    RUN_TEST(test_battery_dims);
    RUN_TEST(test_tap_while_dim_only_wakes);
    RUN_TEST(test_external_never_dims);
    RUN_TEST(test_power_loss_starts_timeout);
    RUN_TEST(test_zero_never_dims);
    return UNITY_END();
}
