#include <unity.h>
#include <string.h>
#include <string>
#include "settings.h"
#include "settings_yaml.h"

static const char *fakePresetName(uint8_t i) { return i == 0 ? "LongFast" : i == 3 ? "LongTurbo" : nullptr; }
static int fakePresetFromName(const char *n) {
    if (!strcmp(n, "LongFast")) return 0;
    if (!strcmp(n, "LongTurbo")) return 3;
    return -1;
}
static const YamlPresetMap kMap{fakePresetName, fakePresetFromName};

void setUp() {}
void tearDown() {}

#define PUT(field, v) (memset(field, 0, sizeof(field)), strcpy(field, v))

static Settings sample() {
    Settings s;
    settingsDefaults(s, 0x1234ABCD);
    PUT(s.longName, "Hill \"Top\" CS");
    PUT(s.shortName, "HILL");
    PUT(s.region, "EU_868");
    s.modemPreset = 3;
    s.freqSlot = 5;
    s.chanCount = 2;
    PUT(s.ch[1].name, "camillia");
    uint8_t len;
    parseKeyBase64("1PG7OiApB1nwvP+rz05pAQ==", s.ch[1].key, len);
    s.ch[1].keyLen = len;
    s.batchSize = 25;
    s.packetGapMs = 4500;
    s.maxHops = 4;
    s.mqttEnabled = true;
    PUT(s.mqttHost, "mqtt.example.org");
    s.mqttPort = 8883;
    PUT(s.mqttUser, "me");
    PUT(s.mqttPass, "p#ss: word\\x");
    PUT(s.mqttRoot, "msh/EU_868");
    PUT(s.staSsid, "Home WiFi");
    PUT(s.staPass, "secret");
    PUT(s.tz, "CET-1CEST,M3.5.0,M10.5.0/3");
    return s;
}

void test_yaml_round_trip() {
    Settings a = sample();
    std::string y = settingsToYaml(a, kMap);
    TEST_ASSERT_NOT_NULL(strstr(y.c_str(), "preset: LongTurbo"));
    TEST_ASSERT_NOT_NULL(strstr(y.c_str(), "key: \"1PG7OiApB1nwvP+rz05pAQ==\""));

    Settings b;
    settingsDefaults(b, 1);
    char err[96] = "";
    TEST_ASSERT_TRUE_MESSAGE(settingsFromYaml(y.c_str(), b, kMap, err, sizeof err), err);
    TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof(Settings));
}

void test_yaml_import_keeps_unlisted_values_and_ignores_comments() {
    Settings b;
    settingsDefaults(b, 1);
    strcpy(b.staSsid, "keep me");
    const char *y =
        "# hand-written\n"
        "identity:\n"
        "  longName: Ridge Server   # trailing comment\n"
        "  shortName: RDG\n"
        "replies:\n"
        "  batchSize: 12\n"
        "unknownSection:\n"
        "  whatever: 1\n";
    char err[96] = "";
    TEST_ASSERT_TRUE_MESSAGE(settingsFromYaml(y, b, kMap, err, sizeof err), err);
    TEST_ASSERT_EQUAL_STRING("Ridge Server", b.longName);
    TEST_ASSERT_EQUAL_STRING("RDG", b.shortName);
    TEST_ASSERT_EQUAL(12, b.batchSize);
    TEST_ASSERT_EQUAL_STRING("keep me", b.staSsid);
    TEST_ASSERT_EQUAL(1, b.chanCount);   // no channels section → unchanged
}

void test_yaml_import_rejects_bad_input_without_changing_settings() {
    Settings b;
    settingsDefaults(b, 1);
    Settings before = b;
    char err[96] = "";

    TEST_ASSERT_FALSE(settingsFromYaml("replies:\n  batchSize: 0\n", b, kMap, err, sizeof err));
    TEST_ASSERT_NOT_NULL(strstr(err, "Batch size"));
    TEST_ASSERT_FALSE(settingsFromYaml("radio:\n  preset: Warp\n", b, kMap, err, sizeof err));
    TEST_ASSERT_NOT_NULL(strstr(err, "preset"));
    TEST_ASSERT_FALSE(settingsFromYaml("channels:\n  - name: x\n    key: \"!!\"\n", b, kMap, err, sizeof err));
    TEST_ASSERT_FALSE(settingsFromYaml(
        "channels:\n  - name: a\n  - name: b\n  - name: c\n  - name: d\n", b, kMap, err, sizeof err));
    TEST_ASSERT_FALSE(settingsFromYaml("replies:\n  batchSize: ten\n", b, kMap, err, sizeof err));
    TEST_ASSERT_EQUAL_MEMORY(&before, &b, sizeof(Settings));
}

void test_yaml_channels_replace_list() {
    Settings b;
    settingsDefaults(b, 1);
    const char *y = "channels:\n  - name: Alpha\n    key: AQ==\n  - name: Beta\n    key: \"\"\n";
    char err[96] = "";
    TEST_ASSERT_TRUE_MESSAGE(settingsFromYaml(y, b, kMap, err, sizeof err), err);
    TEST_ASSERT_EQUAL(2, b.chanCount);
    TEST_ASSERT_EQUAL_STRING("Alpha", b.ch[0].name);
    TEST_ASSERT_EQUAL(1, b.ch[0].keyLen);
    TEST_ASSERT_EQUAL_STRING("Beta", b.ch[1].name);
    TEST_ASSERT_EQUAL(0, b.ch[1].keyLen);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_yaml_round_trip);
    RUN_TEST(test_yaml_import_keeps_unlisted_values_and_ignores_comments);
    RUN_TEST(test_yaml_import_rejects_bad_input_without_changing_settings);
    RUN_TEST(test_yaml_channels_replace_list);
    return UNITY_END();
}
