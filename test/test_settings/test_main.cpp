#include <unity.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "settings.h"

void setUp() {}
void tearDown() {}

void test_defaults() {
    Settings s;
    settingsDefaults(s, 0x1234ABCD);
    TEST_ASSERT_EQUAL_STRING("abcd", s.shortName);
    TEST_ASSERT_EQUAL_STRING("Camillia CS abcd", s.longName);
    TEST_ASSERT_EQUAL_STRING("US", s.region);
    TEST_ASSERT_EQUAL(1, s.chanCount);
    TEST_ASSERT_EQUAL_STRING("LongFast", s.ch[0].name);
    TEST_ASSERT_EQUAL(1, s.ch[0].keyLen);
    TEST_ASSERT_EQUAL_HEX8(0x01, s.ch[0].key[0]);
    TEST_ASSERT_EQUAL(10, s.batchSize);
    TEST_ASSERT_EQUAL(3000, s.packetGapMs);
    TEST_ASSERT_EQUAL(7, s.maxHops);
    TEST_ASSERT_FALSE(s.mqttEnabled);
    TEST_ASSERT_EQUAL_STRING("msh/US", s.mqttRoot);
    TEST_ASSERT_EQUAL(1883, s.mqttPort);
    TEST_ASSERT_EQUAL_STRING("EST5EDT,M3.2.0,M11.1.0", s.tz);
    char err[96];
    TEST_ASSERT_TRUE(settingsValidate(s, err, sizeof err));
}

void test_validate_rejects_out_of_range() {
    Settings s;
    char err[96];
    settingsDefaults(s, 1);
    s.batchSize = 0;
    TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));
    TEST_ASSERT_NOT_NULL(strstr(err, "Batch size"));

    settingsDefaults(s, 1); s.batchSize = 51;   TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));
    settingsDefaults(s, 1); s.packetGapMs = 499; TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));
    settingsDefaults(s, 1); s.maxHops = 8;      TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));
    settingsDefaults(s, 1); s.chanCount = 0;    TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));
    settingsDefaults(s, 1); s.chanCount = 11;   TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));
    settingsDefaults(s, 1);
    s.chanCount = 10;
    for (int i = 1; i < 10; i++) { snprintf(s.ch[i].name, sizeof s.ch[i].name, "ch%d", i); s.ch[i].keyLen = 1; s.ch[i].key[0] = 1; }
    TEST_ASSERT_TRUE_MESSAGE(settingsValidate(s, err, sizeof err), err);   // 10 channels allowed
    settingsDefaults(s, 1); s.shortName[0] = 0; TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));
}

void test_validate_channels() {
    Settings s;
    char err[96];
    settingsDefaults(s, 1);
    s.chanCount = 2;
    strcpy(s.ch[1].name, "LongFast");
    s.ch[1].keyLen = 1; s.ch[1].key[0] = 1;
    TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));   // duplicate name
    strcpy(s.ch[1].name, "");
    TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));   // empty name
    strcpy(s.ch[1].name, "camillia");
    s.ch[1].keyLen = 5;
    TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));   // bad key length
    s.ch[1].keyLen = 32;
    TEST_ASSERT_TRUE(settingsValidate(s, err, sizeof err));
    strcpy(s.ch[1].name, "camillia-cs");
    TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));   // reserved discovery name (and too long)
}

void test_key_base64_round_trip() {
    uint8_t key[32];
    uint8_t len = 0;
    TEST_ASSERT_TRUE(parseKeyBase64("AQ==", key, len));
    TEST_ASSERT_EQUAL(1, len);
    TEST_ASSERT_EQUAL_HEX8(0x01, key[0]);

    TEST_ASSERT_TRUE(parseKeyBase64("1PG7OiApB1nwvP+rz05pAQ==", key, len));
    TEST_ASSERT_EQUAL(16, len);
    TEST_ASSERT_EQUAL_HEX8(0xd4, key[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, key[15]);

    char out[48];
    formatKeyBase64(key, len, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("1PG7OiApB1nwvP+rz05pAQ==", out);

    TEST_ASSERT_TRUE(parseKeyBase64("", key, len));   // no encryption
    TEST_ASSERT_EQUAL(0, len);
    TEST_ASSERT_FALSE(parseKeyBase64("!!!!", key, len));
    TEST_ASSERT_FALSE(parseKeyBase64("AAAAAAAA", key, len));   // 6 bytes: not a valid key size
}

void test_display_defaults() {
    Settings s;
    char err[96];
    settingsDefaults(s, 1);
    TEST_ASSERT_EQUAL(160, s.displayBrightness);
    TEST_ASSERT_EQUAL(120, s.displayDimAfterSec);
    TEST_ASSERT_EQUAL(0, s.displayDimLevel);
    TEST_ASSERT_EQUAL(30, s.displayPageSec);
    TEST_ASSERT_TRUE(settingsValidate(s, err, sizeof err));
}

void test_display_ranges() {
    Settings s;
    char err[96];
    settingsDefaults(s, 1); s.displayBrightness = 9;
    TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));
    TEST_ASSERT_NOT_NULL(strstr(err, "brightness"));
    settingsDefaults(s, 1); s.displayDimAfterSec = 3601;
    TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));
    TEST_ASSERT_NOT_NULL(strstr(err, "Dim after"));
    settingsDefaults(s, 1); s.displayDimLevel = 160;
    TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));
    TEST_ASSERT_NOT_NULL(strstr(err, "Dim level"));
    settingsDefaults(s, 1); s.displayPageSec = 9;
    TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));
    TEST_ASSERT_NOT_NULL(strstr(err, "Page time"));
    settingsDefaults(s, 1); s.displayPageSec = 301;
    TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));
    TEST_ASSERT_NOT_NULL(strstr(err, "Page time"));

    settingsDefaults(s, 1);
    s.displayBrightness = 255; s.displayDimLevel = 254; s.displayDimAfterSec = 0; s.displayPageSec = 10;
    TEST_ASSERT_TRUE(settingsValidate(s, err, sizeof err));
}

void test_upgrade_v4_blob() {
    Settings old;
    settingsDefaults(old, 1);
    strcpy(old.longName, "Old");
    old.chanCount = 2;
    const size_t len = SETTINGS_V4_BLOB_LEN;
    uint8_t blob[sizeof(Settings)];
    memcpy(blob, &old, sizeof blob);
    blob[offsetof(Settings, displayBrightness)] = 0xEE;   // stale tail padding in a real v4 blob
    Settings s;
    memset(&s, 0xEE, sizeof s);
    TEST_ASSERT_TRUE(settingsFromBlob(blob, len, 4, s));
    TEST_ASSERT_EQUAL_STRING("Old", s.longName);
    TEST_ASSERT_EQUAL(2, s.chanCount);
    TEST_ASSERT_EQUAL(160, s.displayBrightness);
    TEST_ASSERT_EQUAL(30, s.displayPageSec);
}

void test_blob_current_and_bad() {
    Settings a, b;
    settingsDefaults(a, 1);
    a.displayPageSec = 77;
    strcpy(a.longName, "Cur");
    uint8_t blob[sizeof(Settings)];
    memcpy(blob, &a, sizeof blob);
    TEST_ASSERT_TRUE(settingsFromBlob(blob, sizeof blob, 5, b));
    TEST_ASSERT_EQUAL_STRING("Cur", b.longName);
    TEST_ASSERT_EQUAL(77, b.displayPageSec);
    TEST_ASSERT_FALSE(settingsFromBlob(blob, sizeof blob - 1, 5, b));
    TEST_ASSERT_FALSE(settingsFromBlob(blob, sizeof blob, 4, b));
    TEST_ASSERT_FALSE(settingsFromBlob(blob, SETTINGS_V4_BLOB_LEN, 3, b));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_validate_rejects_out_of_range);
    RUN_TEST(test_validate_channels);
    RUN_TEST(test_key_base64_round_trip);
    RUN_TEST(test_display_defaults);
    RUN_TEST(test_display_ranges);
    RUN_TEST(test_upgrade_v4_blob);
    RUN_TEST(test_blob_current_and_bad);
    return UNITY_END();
}
