#pragma once
// Pure helpers for the OTA updater (ota_update.cpp): version tags, the release
// JSON's tag_name, and release asset names. Native-tested.
#include <stddef.h>

// A SemVer prerelease ("v0.6.0-alpha.1"): anything after a '-'. The chat
// server only follows stable releases, so these are never offered.
bool otaIsPrerelease(const char *tag);

// <0, 0, >0 like strcmp, comparing the numeric components ("v0.10.0" > "0.9.3").
// A prerelease sorts below the release it leads to.
int otaCompareVersions(const char *a, const char *b);

// True when tag has at least one digit, i.e. is a real release version rather
// than a local "dev" build.
bool otaVersionKnown(const char *version);

// Newer stable release than the running one?
bool otaUpdateAvailable(const char *running, const char *latestTag);

// Copies the value of the first "tag_name" string in json into out. False when
// there is none, it is empty, or it does not fit.
bool otaFindTagName(const char *json, char *out, size_t cap);

// "camillia-chat-server-<env>-<tag>-ota.bin"
bool otaAssetName(const char *env, const char *tag, char *out, size_t cap);
