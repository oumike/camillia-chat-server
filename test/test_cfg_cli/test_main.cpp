#include <unity.h>
#include <string.h>
#include <string>
#include "cfg_cli.h"
#include "settings.h"

static int fakePresetFromName(const char *n) {
    if (!strcasecmp(n, "LongFast")) return 0;
    if (!strcasecmp(n, "MediumFast")) return 4;
    return -1;
}
static const char *fakePresetName(uint8_t i) { return i == 0 ? "LongFast" : i == 4 ? "MediumFast" : nullptr; }
static bool fakeRegionKnown(const char *c) { return !strcmp(c, "US") || !strcmp(c, "EU_868"); }
static bool fakeValidate(const Settings &s, char *err, size_t cap) { return settingsValidate(s, err, cap); }

static CfgHooks hooks(bool logo = false) {
    CfgHooks h{};
    h.presetFromName = fakePresetFromName;
    h.presetName = fakePresetName;
    h.regionKnown = fakeRegionKnown;
    h.validate = fakeValidate;
    h.presetList = "LongFast,MediumFast";
    h.hasLogo = logo;
    h.logoW = 200;
    h.logoH = 60;
    return h;
}

static Settings s_live;
static char s_out[256];

static CfgResult run(CfgCli &c, const char *line) { return c.handle(line, s_live, s_out, sizeof s_out); }
static bool starts(const char *prefix) { return strncmp(s_out, prefix, strlen(prefix)) == 0; }

void setUp() { settingsDefaults(s_live, 0x1234ABCD); }
void tearDown() {}

static void test_caps() {
    CfgCli c(hooks());
    run(c, "caps");
    TEST_ASSERT_EQUAL_STRING(
        "[cfg] caps v=1 product=cs firstboot=0 keys=preset,slot,channels,setup,mqtt channels=20 "
        "presets=LongFast,MediumFast",
        s_out);
}

static void test_preset_slot_region() {
    CfgCli c(hooks());
    run(c, "set preset mediumfast");
    TEST_ASSERT_EQUAL_STRING("[cfg] ok preset=MediumFast staged", s_out);
    TEST_ASSERT_EQUAL(4, c.staged().modemPreset);
    run(c, "set preset Nope");
    TEST_ASSERT_TRUE(starts("[cfg] err preset:"));
    run(c, "set slot 51");
    TEST_ASSERT_EQUAL_STRING("[cfg] ok slot=51 staged", s_out);
    TEST_ASSERT_EQUAL(51, c.staged().freqSlot);
    run(c, "set slot 256");
    TEST_ASSERT_TRUE(starts("[cfg] err slot:"));
    run(c, "set region EU_868");
    TEST_ASSERT_EQUAL_STRING("[cfg] ok region=EU_868 staged", s_out);
    run(c, "set region XX");
    TEST_ASSERT_TRUE(starts("[cfg] err region:"));
}

static void test_get_preset() {
    CfgCli c(hooks());
    run(c, "get preset");
    TEST_ASSERT_EQUAL_STRING("[cfg] preset=LongFast", s_out);
}

static void test_names_and_wifi() {
    CfgCli c(hooks());
    run(c, "set longname Hill%20Top");
    TEST_ASSERT_EQUAL_STRING("[cfg] ok longname staged", s_out);
    TEST_ASSERT_EQUAL_STRING("Hill Top", c.staged().longName);
    run(c, "set longname 0123456789012345678901234");
    TEST_ASSERT_TRUE(starts("[cfg] err longname:"));
    run(c, "set shortname %G1");
    TEST_ASSERT_EQUAL_STRING("[cfg] err shortname: bad encoding", s_out);
    run(c, "set shortname T%C3%A9");
    TEST_ASSERT_EQUAL_STRING("[cfg] ok shortname staged", s_out);
    run(c, "set ssid Home");
    TEST_ASSERT_EQUAL_STRING("Home", c.staged().staSsid);
    strcpy(s_live.staPass, "old");
    run(c, "set wifipass");
    TEST_ASSERT_EQUAL_STRING("[cfg] ok wifipass staged", s_out);
    TEST_ASSERT_EQUAL_STRING("", c.staged().staPass);
}

static void test_percent_decode() {
    char out[8];
    TEST_ASSERT_TRUE(cfgPercentDecode("a%2Fb", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("a/b", out);
    TEST_ASSERT_FALSE(cfgPercentDecode("abc%", out, sizeof out));
    TEST_ASSERT_FALSE(cfgPercentDecode("%4", out, sizeof out));
    TEST_ASSERT_FALSE(cfgPercentDecode("12345678", out, sizeof out));   // no room for the terminator
}

static void test_channels() {
    CfgCli c(hooks());
    run(c, "channels clear");
    TEST_ASSERT_EQUAL_STRING("[cfg] ok channels cleared slots=20", s_out);
    run(c, "set channel 0 role=primary name= psk=default pos=1 up=1 down=0");
    TEST_ASSERT_EQUAL_STRING("[cfg] ok channel 0 staged", s_out);
    run(c, "set channel 2 role=secondary name=Gap psk=default");
    TEST_ASSERT_TRUE(starts("[cfg] err channel"));
    run(c, "set channel 1 role=secondary name=Chicago psk=H1HDyEqg5W9XFilicVTIaoWYrs8URmWGo5fAYfbxQgo=");
    TEST_ASSERT_EQUAL_STRING("[cfg] ok channel 1 staged", s_out);
    run(c, "set channel 2 role=secondary name=Open psk=none");
    TEST_ASSERT_EQUAL_STRING("[cfg] ok channel 2 staged", s_out);
    run(c, "set channel 3 role=secondary name=TwelveBytes! psk=default");
    TEST_ASSERT_TRUE(starts("[cfg] err channel"));
    run(c, "set channel 3 role=secondary name=Bad psk=***");
    TEST_ASSERT_TRUE(starts("[cfg] err channel"));

    run(c, "set preset MediumFast");   // after the channels: the primary still follows it
    TEST_ASSERT_EQUAL(CfgResult::Commit, run(c, "commit"));
    TEST_ASSERT_EQUAL_STRING("[cfg] ok committed rebooting", s_out);
    const Settings &s = c.staged();
    TEST_ASSERT_EQUAL(3, s.chanCount);
    TEST_ASSERT_EQUAL_STRING("MediumFast", s.ch[0].name);
    TEST_ASSERT_EQUAL(1, s.ch[0].keyLen);
    TEST_ASSERT_EQUAL(1, s.ch[0].key[0]);
    TEST_ASSERT_EQUAL_STRING("Chicago", s.ch[1].name);
    TEST_ASSERT_EQUAL(32, s.ch[1].keyLen);
    TEST_ASSERT_EQUAL(0, s.ch[2].keyLen);
}

static void test_channels_need_one() {
    CfgCli c(hooks());
    run(c, "channels clear");
    run(c, "commit");
    TEST_ASSERT_TRUE(starts("[cfg] err commit:"));
}

// camillia-mt's form (the flasher's mqttCommand()): every field on one line, and
// a staged broker switches MQTT on.
static void test_mqtt() {
    CfgCli c(hooks());
    run(c, "set mqtt host=mqtt.iowamesh.net port=8882 user=iromesh pass=2Gab9eAz root=iromesh tls=0 enc=0");
    TEST_ASSERT_EQUAL_STRING("[cfg] ok mqtt=mqtt.iowamesh.net:8882 staged", s_out);
    const Settings &s = c.staged();
    TEST_ASSERT_TRUE(s.mqttEnabled);
    TEST_ASSERT_EQUAL_STRING("mqtt.iowamesh.net", s.mqttHost);
    TEST_ASSERT_EQUAL(8882, s.mqttPort);
    TEST_ASSERT_EQUAL_STRING("iromesh", s.mqttUser);
    TEST_ASSERT_EQUAL_STRING("2Gab9eAz", s.mqttPass);
    TEST_ASSERT_EQUAL_STRING("iromesh", s.mqttRoot);

    // Nothing of the previous broker is kept: omitted fields take their defaults.
    run(c, "set mqtt host=mqtt.chimesh.org root=msh%2FUS%2FIL%2FChi");
    TEST_ASSERT_EQUAL_STRING("[cfg] ok mqtt=mqtt.chimesh.org:1883 staged", s_out);
    TEST_ASSERT_EQUAL(1883, c.staged().mqttPort);
    TEST_ASSERT_EQUAL_STRING("", c.staged().mqttUser);
    TEST_ASSERT_EQUAL_STRING("msh/US/IL/Chi", c.staged().mqttRoot);
}

static void test_mqtt_errors() {
    CfgCli c(hooks());
    run(c, "set mqtt host=mqtt.example.org");
    TEST_ASSERT_EQUAL_STRING("[cfg] err mqtt: host and root are required", s_out);
    run(c, "set mqtt host=h root=r port=0");
    TEST_ASSERT_EQUAL_STRING("[cfg] err mqtt: bad port \"0\"", s_out);
    run(c, "set mqtt host=h root=r colour=red");
    TEST_ASSERT_EQUAL_STRING("[cfg] err mqtt: unknown key \"colour\"", s_out);
    run(c, "set mqtt host=h root=r tls=1");
    TEST_ASSERT_EQUAL_STRING("[cfg] err mqtt: TLS is not supported", s_out);
    run(c, "set mqtt host root=r");
    TEST_ASSERT_TRUE(starts("[cfg] err mqtt: expected key=value"));
    TEST_ASSERT_FALSE(c.staged().mqttEnabled);
}

static void test_oktomqtt_accepted() {
    CfgCli c(hooks());
    run(c, "set oktomqtt 1");
    TEST_ASSERT_EQUAL_STRING("[cfg] ok oktomqtt=1 staged", s_out);
    run(c, "set oktomqtt 2");
    TEST_ASSERT_EQUAL_STRING("[cfg] err oktomqtt: expected 0 or 1, got \"2\"", s_out);
}

static void test_role_refused() {
    CfgCli c(hooks());
    run(c, "set role CLIENT");
    TEST_ASSERT_EQUAL_STRING("[cfg] err role: not supported", s_out);
}

static void test_commit_nothing() {
    CfgCli c(hooks());
    TEST_ASSERT_EQUAL(CfgResult::Reply, run(c, "commit"));
    TEST_ASSERT_EQUAL_STRING("[cfg] ok nothing", s_out);
}

static void test_commit_invalid_keeps_staging() {
    CfgCli c(hooks());
    run(c, "channels clear");
    run(c, "set channel 0 role=primary name=Same psk=default");
    run(c, "set channel 1 role=secondary name=Same psk=default");
    TEST_ASSERT_EQUAL(CfgResult::Reply, run(c, "commit"));
    TEST_ASSERT_TRUE(starts("[cfg] err commit: Channel names must be unique"));
    run(c, "abort");
    TEST_ASSERT_EQUAL_STRING("[cfg] ok aborted", s_out);
    run(c, "commit");
    TEST_ASSERT_EQUAL_STRING("[cfg] ok nothing", s_out);
}

static void test_unknown() {
    CfgCli c(hooks());
    run(c, "frob");
    TEST_ASSERT_EQUAL_STRING("[cfg] err frob: unknown", s_out);
    run(c, "set colour red");
    TEST_ASSERT_EQUAL_STRING("[cfg] err colour: unknown", s_out);
}

// ── Logo ─────────────────────────────────────────────────────────────────────

static void test_caps_logo() {
    CfgCli c(hooks(true));
    run(c, "caps");
    TEST_ASSERT_EQUAL_STRING(
        "[cfg] caps v=1 product=cs firstboot=0 keys=preset,slot,channels,setup,mqtt,logo channels=20 "
        "presets=LongFast,MediumFast logo=200x60 logobg=100C29 logomax=8192",
        s_out);
}

static void test_logo_transfer() {
    CfgCli c(hooks(true));
    run(c, "logo begin 8193");
    TEST_ASSERT_TRUE(starts("[cfg] err logo:"));
    run(c, "logo data 00");
    TEST_ASSERT_TRUE(starts("[cfg] err logo:"));

    const uint8_t png[3] = {0x89, 0x50, 0x4e};
    char end[32];
    snprintf(end, sizeof end, "logo end %08x", (unsigned)cfgCrc32(png, 3));
    run(c, "logo begin 3");
    TEST_ASSERT_TRUE(starts("[cfg] ok logo"));
    run(c, "logo data 89504e");
    TEST_ASSERT_TRUE(starts("[cfg] ok logo"));
    run(c, end);
    TEST_ASSERT_EQUAL_STRING("[cfg] ok logo staged", s_out);
    TEST_ASSERT_TRUE(c.logoStaged());
    TEST_ASSERT_EQUAL(3, c.logoLen());
    TEST_ASSERT_EQUAL_MEMORY(png, c.logoData(), 3);
    TEST_ASSERT_EQUAL(CfgResult::Commit, run(c, "commit"));
}

static void test_logo_bad_crc_and_overrun() {
    CfgCli c(hooks(true));
    run(c, "logo begin 3");
    run(c, "logo data 89504e");
    run(c, "logo end 00000000");
    TEST_ASSERT_TRUE(starts("[cfg] err logo:"));
    TEST_ASSERT_FALSE(c.logoStaged());

    run(c, "logo begin 2");
    run(c, "logo data 89504e");
    TEST_ASSERT_TRUE(starts("[cfg] err logo:"));
    TEST_ASSERT_FALSE(c.logoStaged());
}

static void test_logo_clear() {
    CfgCli c(hooks(true));
    run(c, "logo clear");
    TEST_ASSERT_TRUE(starts("[cfg] ok logo"));
    TEST_ASSERT_TRUE(c.logoClear());
    TEST_ASSERT_EQUAL(CfgResult::Commit, run(c, "commit"));
}

static void test_logo_absent() {
    CfgCli c(hooks(false));
    run(c, "logo begin 3");
    TEST_ASSERT_EQUAL_STRING("[cfg] err logo: no logo on this display", s_out);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_caps);
    RUN_TEST(test_preset_slot_region);
    RUN_TEST(test_get_preset);
    RUN_TEST(test_names_and_wifi);
    RUN_TEST(test_percent_decode);
    RUN_TEST(test_channels);
    RUN_TEST(test_channels_need_one);
    RUN_TEST(test_mqtt);
    RUN_TEST(test_mqtt_errors);
    RUN_TEST(test_oktomqtt_accepted);
    RUN_TEST(test_role_refused);
    RUN_TEST(test_commit_nothing);
    RUN_TEST(test_commit_invalid_keeps_staging);
    RUN_TEST(test_unknown);
    RUN_TEST(test_caps_logo);
    RUN_TEST(test_logo_transfer);
    RUN_TEST(test_logo_bad_crc_and_overrun);
    RUN_TEST(test_logo_clear);
    RUN_TEST(test_logo_absent);
    return UNITY_END();
}
