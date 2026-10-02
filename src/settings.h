#pragma once
// Server settings (spec §5.5). Pure part: defaults, validation, key parsing.
// NVS load/save lives in settings_nvs.cpp (device only).
#include <stddef.h>
#include <stdint.h>

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
    ChannelCfg ch[3];
    uint8_t    batchSize;
    uint16_t   packetGapMs;
    uint8_t    maxHops;
    bool       mqttEnabled;
    char       mqttHost[64];
    uint16_t   mqttPort;
    char       mqttUser[32];
    char       mqttPass[64];
    char       mqttRoot[48];
    char       apPass[64];    // empty = open AP; else >= 8 chars
    char       staSsid[33];
    char       staPass[64];
};

void settingsDefaults(Settings &s, uint32_t nodeId);
// False with a human-readable reason in err if anything is out of range.
bool settingsValidate(const Settings &s, char *err, size_t errCap);

// Meshtastic-style base64 PSK ("AQ==", 16 or 32 bytes, or empty for none).
bool parseKeyBase64(const char *in, uint8_t key[32], uint8_t &len);
void formatKeyBase64(const uint8_t *key, uint8_t len, char *out, size_t cap);

// Device only (settings_nvs.cpp).
bool settingsLoad(Settings &s);
void settingsSave(const Settings &s);
