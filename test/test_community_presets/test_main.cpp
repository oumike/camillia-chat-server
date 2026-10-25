#include <unity.h>
#include <string.h>
#include <strings.h>
#include "cfg_cli.h"
#include "community_preset_lines.h"
#include "community_presets.h"
#include "settings.h"

// Every preset name and region a profile can use, as the device's tables would
// accept them (the radio's own limits are communityPresetProblem()'s business).
static const char *const kNames[] = {"LongFast", "LongMod", "LongSlow", "LongTurbo", "MediumFast", "MediumSlow",
                                     "ShortFast", "ShortSlow", "ShortTurbo", "LiteFast", "LiteSlow", "NarrowFast",
                                     "NarrowSlow", "TinyFast", "TinySlow", "MediumTurbo"};
static int presetFromName(const char *n) {
    for (int i = 0; i < 16; i++)
        if (!strcasecmp(kNames[i], n)) return i;
    return -1;
}
static const char *presetName(uint8_t i) { return i < 16 ? kNames[i] : nullptr; }
static bool regionKnown(const char *) { return true; }
static bool validate(const Settings &s, char *err, size_t cap) { return settingsValidate(s, err, cap); }

static CfgHooks hooks() {
    CfgHooks h{};
    h.presetFromName = presetFromName;
    h.presetName = presetName;
    h.regionKnown = regionKnown;
    h.validate = validate;
    h.presetList = "";
    return h;
}

void setUp() {}
void tearDown() {}

static int find(const char *id) {
    for (int i = 0; i < kCommunityPresetCount; i++)
        if (!strcmp(kCommunityPresets[i].id, id)) return i;
    return -1;
}

// Runs preset idx's lines through a CfgCli; the last must commit.
static Settings applied(int idx, const Settings &live) {
    CfgCli cli(hooks());
    CfgPresetLine lines[CFG_PRESET_MAX_LINES];
    const int n = communityPresetLines(idx, lines, CFG_PRESET_MAX_LINES);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING("commit", lines[n - 1].text);
    char out[256];
    for (int i = 0; i < n; i++) {
        CfgResult r = cli.handle(lines[i].text, live, out, sizeof out);
        TEST_ASSERT_TRUE_MESSAGE(strncmp(out, "[cfg] err", 9) != 0, out);
        if (i == n - 1) TEST_ASSERT_EQUAL(CfgResult::Commit, r);
    }
    return cli.staged();
}

static void test_every_preset_commits() {
    Settings live;
    settingsDefaults(live, 1);
    TEST_ASSERT_GREATER_THAN(0, kCommunityPresetCount);
    for (int i = 0; i < kCommunityPresetCount; i++) {
        const Settings s = applied(i, live);
        TEST_ASSERT_EQUAL_MESSAGE(kCommunityPresets[i].channelCount, s.chanCount, kCommunityPresets[i].id);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(kCommunityPresets[i].region, s.region, kCommunityPresets[i].id);
    }
}

static void test_chimesh() {
    const int i = find("chimesh");
    TEST_ASSERT_GREATER_OR_EQUAL(0, i);
    Settings live;
    settingsDefaults(live, 1);
    const Settings s = applied(i, live);
    TEST_ASSERT_EQUAL_STRING("LongFast", s.ch[0].name);
    TEST_ASSERT_EQUAL_STRING("Chicago", s.ch[1].name);
    TEST_ASSERT_EQUAL(32, s.ch[1].keyLen);
    TEST_ASSERT_TRUE(s.mqttEnabled);
    TEST_ASSERT_EQUAL_STRING("mqtt.chimesh.org", s.mqttHost);
    TEST_ASSERT_EQUAL_STRING("msh/US/IL/Chi", s.mqttRoot);
}

static void test_no_broker_keeps_mqtt() {
    int idx = -1;
    for (int i = 0; i < kCommunityPresetCount && idx < 0; i++)
        if (!kCommunityPresets[i].mqtt.host[0]) idx = i;
    TEST_ASSERT_GREATER_OR_EQUAL(0, idx);
    Settings live;
    settingsDefaults(live, 1);
    live.mqttEnabled = true;
    strcpy(live.mqttHost, "mine.example.org");
    const Settings s = applied(idx, live);
    TEST_ASSERT_TRUE(s.mqttEnabled);
    TEST_ASSERT_EQUAL_STRING("mine.example.org", s.mqttHost);
}

static void test_slot_pinned() {
    int idx = -1;
    for (int i = 0; i < kCommunityPresetCount && idx < 0; i++)
        if (kCommunityPresets[i].slot && kCommunityPresets[i].preset[0]) idx = i;
    TEST_ASSERT_GREATER_OR_EQUAL(0, idx);
    Settings live;
    settingsDefaults(live, 1);
    TEST_ASSERT_EQUAL(kCommunityPresets[idx].slot, applied(idx, live).freqSlot);
}

static void test_bad_index() {
    CfgPresetLine lines[CFG_PRESET_MAX_LINES];
    TEST_ASSERT_EQUAL(0, communityPresetLines(-1, lines, CFG_PRESET_MAX_LINES));
    TEST_ASSERT_EQUAL(0, communityPresetLines(kCommunityPresetCount, lines, CFG_PRESET_MAX_LINES));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_every_preset_commits);
    RUN_TEST(test_chimesh);
    RUN_TEST(test_no_broker_keeps_mqtt);
    RUN_TEST(test_slot_pinned);
    RUN_TEST(test_bad_index);
    return UNITY_END();
}
