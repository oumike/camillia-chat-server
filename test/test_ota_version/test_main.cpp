#include <unity.h>
#include "ota_version.h"

void setUp() {}
void tearDown() {}

void test_compare_versions() {
    TEST_ASSERT_TRUE(otaCompareVersions("0.5.1", "v0.6.0") < 0);
    TEST_ASSERT_TRUE(otaCompareVersions("0.10.0", "v0.9.9") > 0);
    TEST_ASSERT_EQUAL(0, otaCompareVersions("0.5.1", "v0.5.1"));
    TEST_ASSERT_TRUE(otaCompareVersions("0.5", "v0.5.1") < 0);
    TEST_ASSERT_TRUE(otaCompareVersions("v0.6.0-alpha.3", "v0.6.0") < 0);
    TEST_ASSERT_TRUE(otaCompareVersions("v0.6.0-alpha.10", "v0.6.0-alpha.9") > 0);
}

void test_update_available_stable_only() {
    TEST_ASSERT_TRUE(otaUpdateAvailable("0.5.1", "v0.5.2"));
    TEST_ASSERT_FALSE(otaUpdateAvailable("0.5.1", "v0.5.1"));
    TEST_ASSERT_FALSE(otaUpdateAvailable("0.5.2", "v0.5.1"));          // never downgrade
    TEST_ASSERT_FALSE(otaUpdateAvailable("0.5.1", "v0.6.0-alpha.1"));  // never a prerelease
    TEST_ASSERT_FALSE(otaUpdateAvailable("0.5.1", ""));
    TEST_ASSERT_FALSE(otaVersionKnown("dev"));
    TEST_ASSERT_TRUE(otaVersionKnown("0.5.1"));
}

void test_find_tag_name() {
    char tag[32];
    TEST_ASSERT_TRUE(otaFindTagName("{\"url\":\"x\",\"tag_name\" : \"v0.6.0\",\"name\":\"y\"}", tag, sizeof tag));
    TEST_ASSERT_EQUAL_STRING("v0.6.0", tag);
    TEST_ASSERT_FALSE(otaFindTagName("{\"name\":\"v1\"}", tag, sizeof tag));
    TEST_ASSERT_FALSE(otaFindTagName("{\"tag_name\":\"\"}", tag, sizeof tag));
    TEST_ASSERT_FALSE(otaFindTagName("{\"tag_name\":\"../x\"}", tag, sizeof tag));   // not URL-safe
    TEST_ASSERT_FALSE(otaFindTagName("{\"tag_name\":\"v0.6.0", tag, sizeof tag));   // cut off
    char small[4];
    TEST_ASSERT_FALSE(otaFindTagName("{\"tag_name\":\"v0.6.0\"}", small, sizeof small));
}

void test_asset_name() {
    char name[96];
    TEST_ASSERT_TRUE(otaAssetName("wio-tracker-l2", "v0.6.0", name, sizeof name));
    TEST_ASSERT_EQUAL_STRING("camillia-chat-server-wio-tracker-l2-v0.6.0-ota.bin", name);
    TEST_ASSERT_FALSE(otaAssetName("", "v0.6.0", name, sizeof name));
    TEST_ASSERT_FALSE(otaAssetName("heltec-v4", "v0.6.0", name, 10));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_compare_versions);
    RUN_TEST(test_update_available_stable_only);
    RUN_TEST(test_find_tag_name);
    RUN_TEST(test_asset_name);
    return UNITY_END();
}
