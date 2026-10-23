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
    settingsDefaults(s, 1); s.chanCount = 21;   TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));
    settingsDefaults(s, 1);
    s.chanCount = 20;
    for (int i = 1; i < 20; i++) { snprintf(s.ch[i].name, sizeof s.ch[i].name, "ch%d", i); s.ch[i].keyLen = 1; s.ch[i].key[0] = 1; }
    TEST_ASSERT_TRUE_MESSAGE(settingsValidate(s, err, sizeof err), err);   // 20 channels allowed
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

// A blob in the old 10-slot layout, with every field after the channels set.
static void oldBlob(SettingsV6 &o) {
    memset(&o, 0, sizeof o);
    strcpy(o.shortName, "OLD"); strcpy(o.longName, "Old"); strcpy(o.region, "EU_868");
    o.modemPreset = 4; o.chanCount = 10;
    for (int i = 0; i < 10; i++) {
        snprintf(o.ch[i].name, sizeof o.ch[i].name, "c%d", i);
        o.ch[i].keyLen = 1; o.ch[i].key[0] = (uint8_t)(i + 1);
    }
    o.batchSize = 12; o.packetGapMs = 4000; o.maxHops = 5; o.mqttEnabled = true;
    strcpy(o.mqttHost, "broker"); o.mqttPort = 1884; strcpy(o.mqttRoot, "msh/EU");
    strcpy(o.staSsid, "Home"); strcpy(o.tz, "UTC0");
    o.displayBrightness = 99; o.displayDimAfterSec = 60; o.displayDimLevel = 7; o.displayPageSec = 45;
    o.otaAutoInstall = true;
}

static void assertOldCarried(const Settings &s) {
    TEST_ASSERT_EQUAL_STRING("Old", s.longName);
    TEST_ASSERT_EQUAL_STRING("EU_868", s.region);
    TEST_ASSERT_EQUAL(4, s.modemPreset);
    TEST_ASSERT_EQUAL(10, s.chanCount);
    TEST_ASSERT_EQUAL_STRING("c9", s.ch[9].name);
    TEST_ASSERT_EQUAL(10, s.ch[9].key[0]);
    TEST_ASSERT_EQUAL_STRING("", s.ch[10].name);
    TEST_ASSERT_EQUAL(12, s.batchSize);
    TEST_ASSERT_EQUAL(4000, s.packetGapMs);
    TEST_ASSERT_EQUAL(5, s.maxHops);
    TEST_ASSERT_TRUE(s.mqttEnabled);
    TEST_ASSERT_EQUAL_STRING("broker", s.mqttHost);
    TEST_ASSERT_EQUAL(1884, s.mqttPort);
    TEST_ASSERT_EQUAL_STRING("Home", s.staSsid);
    TEST_ASSERT_EQUAL_STRING("UTC0", s.tz);
}

void test_upgrade_v4_blob() {
    SettingsV6 o;
    oldBlob(o);
    Settings s;
    memset(&s, 0xEE, sizeof s);
    TEST_ASSERT_TRUE(settingsFromBlob((const uint8_t *)&o, SETTINGS_V4_BLOB_LEN, 4, s));
    assertOldCarried(s);
    TEST_ASSERT_EQUAL(160, s.displayBrightness);   // v4 predates these: defaults
    TEST_ASSERT_EQUAL(30, s.displayPageSec);
    TEST_ASSERT_FALSE(s.otaAutoInstall);
}

void test_upgrade_v5_blob() {
    SettingsV6 o;
    oldBlob(o);
    Settings s;
    memset(&s, 0xEE, sizeof s);
    TEST_ASSERT_TRUE(settingsFromBlob((const uint8_t *)&o, SETTINGS_V5_BLOB_LEN, 5, s));
    assertOldCarried(s);
    TEST_ASSERT_EQUAL(99, s.displayBrightness);
    TEST_ASSERT_EQUAL(45, s.displayPageSec);
    TEST_ASSERT_FALSE(s.otaAutoInstall);
}

void test_upgrade_v6_blob() {
    SettingsV6 o;
    oldBlob(o);
    Settings s;
    memset(&s, 0xEE, sizeof s);
    TEST_ASSERT_TRUE(settingsFromBlob((const uint8_t *)&o, SETTINGS_V6_BLOB_LEN, 6, s));
    assertOldCarried(s);
    TEST_ASSERT_EQUAL(60, s.displayDimAfterSec);
    TEST_ASSERT_EQUAL(7, s.displayDimLevel);
    TEST_ASSERT_TRUE(s.otaAutoInstall);
    TEST_ASSERT_FALSE(settingsFromBlob((const uint8_t *)&o, SETTINGS_V6_BLOB_LEN - 1, 6, s));
}

void test_blob_current_and_bad() {
    Settings a, b;
    settingsDefaults(a, 1);
    a.displayPageSec = 77;
    strcpy(a.longName, "Cur");
    uint8_t blob[sizeof(Settings)];
    memcpy(blob, &a, sizeof blob);
    a.otaAutoInstall = true;
    memcpy(blob, &a, sizeof blob);
    TEST_ASSERT_TRUE(settingsFromBlob(blob, sizeof blob, 8, b));
    TEST_ASSERT_EQUAL_STRING("Cur", b.longName);
    TEST_ASSERT_EQUAL(77, b.displayPageSec);
    TEST_ASSERT_TRUE(b.otaAutoInstall);
    TEST_ASSERT_FALSE(settingsFromBlob(blob, sizeof blob - 1, 8, b));
    TEST_ASSERT_FALSE(settingsFromBlob(blob, sizeof blob, 6, b));
    TEST_ASSERT_FALSE(settingsFromBlob(blob, sizeof blob, 4, b));
    TEST_ASSERT_FALSE(settingsFromBlob(blob, SETTINGS_V4_BLOB_LEN, 3, b));
}

void test_upgrade_v7_blob() {
    Settings a, b;
    settingsDefaults(a, 1);
    strcpy(a.longName, "Seven");
    a.otaAutoInstall = true;
    a.msgsPerChannel = 0xBEEF;   // not part of a v7 blob
    memset(&b, 0xEE, sizeof b);
    TEST_ASSERT_TRUE(settingsFromBlob((const uint8_t *)&a, SETTINGS_V7_BLOB_LEN, 7, b));
    TEST_ASSERT_EQUAL_STRING("Seven", b.longName);
    TEST_ASSERT_TRUE(b.otaAutoInstall);
    TEST_ASSERT_EQUAL(250, b.msgsPerChannel);
    TEST_ASSERT_FALSE(settingsFromBlob((const uint8_t *)&a, sizeof(Settings), 7, b));
}

void test_msgs_per_channel_range() {
    Settings s;
    settingsDefaults(s, 1);
    char err[96] = "";
    TEST_ASSERT_EQUAL(250, s.msgsPerChannel);
    s.msgsPerChannel = 9;
    TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));
    TEST_ASSERT_NOT_NULL(strstr(err, "Messages per channel"));
    s.msgsPerChannel = 1001;
    TEST_ASSERT_FALSE(settingsValidate(s, err, sizeof err));
    s.msgsPerChannel = 10;
    TEST_ASSERT_TRUE(settingsValidate(s, err, sizeof err));
    s.msgsPerChannel = 1000;
    TEST_ASSERT_TRUE(settingsValidate(s, err, sizeof err));
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
    RUN_TEST(test_upgrade_v5_blob);
    RUN_TEST(test_upgrade_v6_blob);
    RUN_TEST(test_upgrade_v7_blob);
    RUN_TEST(test_blob_current_and_bad);
    RUN_TEST(test_msgs_per_channel_range);
    return UNITY_END();
}
