#include <unity.h>
#include "battery_level.h"

void setUp() {}
void tearDown() {}

void test_percent_curve_matches_camillia_mt() {
    TEST_ASSERT_EQUAL(100, batteryPercentFromVolts(4.25f));
    TEST_ASSERT_EQUAL(100, batteryPercentFromVolts(4.20f));
    TEST_ASSERT_EQUAL(55, batteryPercentFromVolts(3.95f));
    TEST_ASSERT_EQUAL(50, batteryPercentFromVolts(3.93f));   // midway 3.91 (45) .. 3.95 (55)
    TEST_ASSERT_EQUAL(0, batteryPercentFromVolts(3.70f));
    TEST_ASSERT_EQUAL(0, batteryPercentFromVolts(3.50f));
}

void test_battery_state_from_volts() {
    TEST_ASSERT_EQUAL(BATT_ABSENT, batteryStateFromVolts(0.4f));    // nothing on the JST
    TEST_ASSERT_EQUAL(BATT_EXTERNAL, batteryStateFromVolts(4.40f)); // USB / charging, no cell reading
    TEST_ASSERT_EQUAL(BATT_PRESENT, batteryStateFromVolts(3.90f));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_percent_curve_matches_camillia_mt);
    RUN_TEST(test_battery_state_from_volts);
    return UNITY_END();
}
