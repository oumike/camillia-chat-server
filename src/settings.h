#pragma once
// Server settings (spec §5.5). Pure part: defaults, validation, key parsing.
// NVS load/save lives in settings_nvs.cpp (device only).
#include <stddef.h>
#include <stdint.h>

constexpr int SETTINGS_MAX_CHANNELS = 20;

struct ChannelCfg {
    char    name[12];
    uint8_t key[32];
    uint8_t keyLen;   // 0 = none, 1 = Meshtastic short PSK, 16 / 32 = AES
};

struct Settings {
    char       shortName[5];
    char       longName[25];
    char       region[12];
    uint8_t    modemPreset;   // ModemPreset index (mesh_channel_plan.h)
    uint8_t    freqSlot;      // 0 = auto (hash of the preset's channel name)
    uint8_t    chanCount;
    ChannelCfg ch[SETTINGS_MAX_CHANNELS];
    uint8_t    batchSize;
    uint16_t   packetGapMs;
    uint8_t    maxHops;
    bool       mqttEnabled;
    char       mqttHost[64];
    uint16_t   mqttPort;
    char       mqttUser[32];
    char       mqttPass[64];
    char       mqttRoot[48];
    char       staSsid[33];
    char       staPass[64];
    char       tz[48];        // POSIX TZ string for the OLED clock
    // Display settings (TFT expansion build; stored and validated on every build).
    uint8_t    displayBrightness;    // 10-255
    uint16_t   displayDimAfterSec;   // 0 = never dim
    uint8_t    displayDimLevel;      // 0-255, below displayBrightness
    uint16_t   displayPageSec;       // 10-300
    bool       otaAutoInstall;       // install a newer signed release without asking
    uint16_t   msgsPerChannel;       // v8: messages kept per channel (10-1000)
};

// The NVS layout of versions 4-6, frozen: 10 channel slots. Never edit it; it is
// only read, to upgrade an old blob.
struct SettingsV6 {
    char       shortName[5];
    char       longName[25];
    char       region[12];
    uint8_t    modemPreset;
    uint8_t    freqSlot;
    uint8_t    chanCount;
    ChannelCfg ch[10];
    uint8_t    batchSize;
    uint16_t   packetGapMs;
    uint8_t    maxHops;
    bool       mqttEnabled;
    char       mqttHost[64];
    uint16_t   mqttPort;
    char       mqttUser[32];
    char       mqttPass[64];
    char       mqttRoot[48];
    char       staSsid[33];
    char       staPass[64];
    char       tz[48];
    uint8_t    displayBrightness;    // v5+
    uint16_t   displayDimAfterSec;
    uint8_t    displayDimLevel;
    uint16_t   displayPageSec;
    bool       otaAutoInstall;       // v6
};

// An old struct's sizeof was padded up to its alignment, so a blob that ended
// at a field is offsetof() rounded up to that.
constexpr size_t settingsBlobLen(size_t end) {
    return (end + alignof(SettingsV6) - 1) / alignof(SettingsV6) * alignof(SettingsV6);
}
constexpr size_t SETTINGS_V4_BLOB_LEN = settingsBlobLen(offsetof(SettingsV6, displayBrightness));
constexpr size_t SETTINGS_V5_BLOB_LEN = settingsBlobLen(offsetof(SettingsV6, otaAutoInstall));
constexpr size_t SETTINGS_V6_BLOB_LEN = sizeof(SettingsV6);
// Version 7 is the current layout up to otaAutoInstall.
constexpr size_t SETTINGS_V7_END = offsetof(Settings, otaAutoInstall) + sizeof(bool);
constexpr size_t SETTINGS_V7_BLOB_LEN =
    (SETTINGS_V7_END + alignof(Settings) - 1) / alignof(Settings) * alignof(Settings);

void settingsDefaults(Settings &s, uint32_t nodeId);
// False with a human-readable reason in err if anything is out of range.
bool settingsValidate(const Settings &s, char *err, size_t errCap);

// Meshtastic-style base64 PSK ("AQ==", 16 or 32 bytes, or empty for none).
bool parseKeyBase64(const char *in, uint8_t key[32], uint8_t &len);
void formatKeyBase64(const uint8_t *key, uint8_t len, char *out, size_t cap);

// Rebuild Settings from a stored NVS blob (pure). Version 8 is the current layout;
// 7 predates msgsPerChannel; 4-6 had 10 channel slots and predate later fields
// (v4: display, v5: OTA). Missing fields take their defaults.
bool settingsFromBlob(const uint8_t *blob, size_t len, uint8_t ver, Settings &out);

// Device only (settings_nvs.cpp).
bool settingsLoad(Settings &s);
void settingsSave(const Settings &s);
