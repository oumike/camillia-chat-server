#include <unity.h>
#include <string.h>
#include "activity_log.h"

void setUp() {}
void tearDown() {}

static ActivityEntry mk(uint32_t a) {
    ActivityEntry e = {};
    e.a = a;
    return e;
}

void test_wraps_at_cap() {
    ActivityLog log;
    for (uint32_t i = 0; i < 60; i++) log.add(mk(i));
    TEST_ASSERT_EQUAL(ACTIVITY_CAP, log.count());
    ActivityEntry out[ACTIVITY_CAP];
    TEST_ASSERT_EQUAL(ACTIVITY_CAP, log.copyNewest(out, ACTIVITY_CAP));
    for (int i = 0; i < ACTIVITY_CAP; i++) TEST_ASSERT_EQUAL_UINT32(59 - i, out[i].a);
}

void test_copy_max_smaller() {
    ActivityLog log;
    for (uint32_t i = 0; i < 5; i++) log.add(mk(i));
    ActivityEntry out[3];
    TEST_ASSERT_EQUAL(3, log.copyNewest(out, 3));
    TEST_ASSERT_EQUAL_UINT32(4, out[0].a);
    TEST_ASSERT_EQUAL_UINT32(2, out[2].a);
    TEST_ASSERT_EQUAL(0, log.copyNewest(out, 0));
    ActivityLog empty;
    TEST_ASSERT_EQUAL(0, empty.count());
    TEST_ASSERT_EQUAL(0, empty.copyNewest(out, 3));
}

static void check(const ActivityEntry &e, const char *chan, const char *want) {
    char out[64];
    activityDetail(e, chan, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(want, out);
}

void test_detail_text() {
    ActivityEntry e = {};
    e.kind = ACT_DISCOVER;
    check(e, nullptr, "DISCOVER");
    e.kind = ACT_ANNOUNCE;
    check(e, nullptr, "ANNOUNCE sent");
    e.kind = ACT_REQUEST;
    e.a = 41;
    check(e, "LongFast", "REQUEST #LongFast from seq 41");
    e.a = 0;
    check(e, "LongFast", "REQUEST #LongFast from start");
    check(e, nullptr, "REQUEST #? from start");
    check(e, "", "REQUEST #? from start");
    e.kind = ACT_BATCH;
    e.a = 3; e.b = 24; e.flags = 1;
    check(e, nullptr, "sent 3 packets (24 msgs), MORE");
    e.a = 1; e.b = 1; e.flags = 0;
    check(e, nullptr, "sent 1 packet (1 msg)");
    e.kind = ACT_HELD;
    check(e, nullptr, "held: airtime limit");
    e.kind = ACT_TX_FAIL;
    check(e, nullptr, "send FAILED");
}

void test_detail_terminates() {
    ActivityEntry e = {};
    e.kind = ACT_REQUEST;
    e.a = 41;
    char out[8];
    memset(out, 'x', sizeof(out));
    activityDetail(e, "LongFast", out, sizeof(out));
    TEST_ASSERT_EQUAL(7, (int)strlen(out));
    activityDetail(e, "LongFast", out, 0);  // must not write
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_wraps_at_cap);
    RUN_TEST(test_copy_max_smaller);
    RUN_TEST(test_detail_text);
    RUN_TEST(test_detail_terminates);
    return UNITY_END();
}
