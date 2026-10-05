#include <unity.h>
#include "wake_edge.h"

void setUp() {}
void tearDown() {}

static void singlePressFrom(uint32_t t0) {
    WakeEdge w;
    TEST_ASSERT_FALSE(w.update(false, t0));
    TEST_ASSERT_FALSE(w.update(true, t0 + 10));
    TEST_ASSERT_TRUE(w.update(true, t0 + 45));
    TEST_ASSERT_FALSE(w.update(true, t0 + 60));
    TEST_ASSERT_FALSE(w.update(true, t0 + 2000));
}

void test_single_press() { singlePressFrom(0); }

void test_bounce_ignored() {
    WakeEdge w;
    TEST_ASSERT_FALSE(w.update(true, 0));
    TEST_ASSERT_FALSE(w.update(false, 10));
    TEST_ASSERT_FALSE(w.update(true, 20));
    TEST_ASSERT_FALSE(w.update(false, 25));
    TEST_ASSERT_FALSE(w.update(false, 100));
}

void test_two_presses() {
    WakeEdge w;
    TEST_ASSERT_FALSE(w.update(true, 100));
    TEST_ASSERT_TRUE(w.update(true, 130));
    TEST_ASSERT_FALSE(w.update(false, 200));
    TEST_ASSERT_FALSE(w.update(false, 230));   // release is stable, no edge reported
    TEST_ASSERT_FALSE(w.update(true, 300));
    TEST_ASSERT_TRUE(w.update(true, 330));
}

void test_short_release_does_not_repeat() {
    WakeEdge w;
    TEST_ASSERT_FALSE(w.update(true, 0));
    TEST_ASSERT_TRUE(w.update(true, 30));
    TEST_ASSERT_FALSE(w.update(false, 40));
    TEST_ASSERT_FALSE(w.update(true, 50));
    TEST_ASSERT_FALSE(w.update(true, 500));
}

void test_wraparound() { singlePressFrom(0xFFFFFFF0u); }

void test_busy() {
    WakeEdge w;
    TEST_ASSERT_FALSE(w.busy());
    w.update(true, 0);
    TEST_ASSERT_TRUE(w.busy());
    w.update(true, 30);
    w.update(false, 40);
    TEST_ASSERT_TRUE(w.busy());
    w.update(false, 70);
    TEST_ASSERT_FALSE(w.busy());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_single_press);
    RUN_TEST(test_bounce_ignored);
    RUN_TEST(test_two_presses);
    RUN_TEST(test_short_release_does_not_repeat);
    RUN_TEST(test_wraparound);
    RUN_TEST(test_busy);
    return UNITY_END();
}
