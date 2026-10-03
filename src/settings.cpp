#include "settings.h"
#include <stdio.h>
#include <string.h>
#include "cs_proto.h"

void settingsDefaults(Settings &s, uint32_t nodeId) {
    memset(&s, 0, sizeof(s));
    snprintf(s.shortName, sizeof s.shortName, "%04x", (unsigned)(nodeId & 0xFFFF));
    snprintf(s.longName, sizeof s.longName, "Camillia CS %s", s.shortName);
    strcpy(s.region, "US");
    s.modemPreset = 0;   // PRESET_LONG_FAST
    s.freqSlot = 0;
    s.chanCount = 1;
    strcpy(s.ch[0].name, "LongFast");
    s.ch[0].key[0] = 0x01;
    s.ch[0].keyLen = 1;
    s.batchSize = 10;
    s.packetGapMs = 3000;
    s.maxHops = 7;
    s.mqttEnabled = false;
    s.mqttPort = 1883;
    strcpy(s.mqttRoot, "msh/US");
    strcpy(s.tz, "EST5EDT,M3.2.0,M11.1.0");
}

static bool fail(char *err, size_t cap, const char *msg) {
    snprintf(err, cap, "%s", msg);
    return false;
}

bool settingsValidate(const Settings &s, char *err, size_t cap) {
    if (!s.shortName[0]) return fail(err, cap, "Short name is required");
    if (!s.longName[0]) return fail(err, cap, "Node name is required");
    if (s.chanCount < 1 || s.chanCount > SETTINGS_MAX_CHANNELS)
        return fail(err, cap, "Number of channels must be 1-10");
    for (int i = 0; i < s.chanCount; i++) {
        const ChannelCfg &c = s.ch[i];
        if (!c.name[0]) return fail(err, cap, "Every channel needs a name");
        if (!strcmp(c.name, csp::DISCOVERY_CHANNEL_NAME))
            return fail(err, cap, "camillia-cs is reserved for discovery");
        if (c.keyLen != 0 && c.keyLen != 1 && c.keyLen != 16 && c.keyLen != 32)
            return fail(err, cap, "Channel keys must be 1, 16 or 32 bytes");
        for (int j = 0; j < i; j++)
            if (!strcmp(c.name, s.ch[j].name)) return fail(err, cap, "Channel names must be unique");
    }
    if (s.batchSize < 1 || s.batchSize > 50) return fail(err, cap, "Batch size must be 1-50");
    if (s.packetGapMs < 500 || s.packetGapMs > 60000)
        return fail(err, cap, "Packet gap must be 500-60000 ms");
    if (s.maxHops > 7) return fail(err, cap, "Max hops must be 0-7");
    if (s.mqttEnabled && !s.mqttHost[0]) return fail(err, cap, "MQTT broker is required when MQTT is on");
    return true;
}

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64Val(char c) {
    const char *p = c ? strchr(kB64, c) : nullptr;
    return p ? (int)(p - kB64) : -1;
}

bool parseKeyBase64(const char *in, uint8_t key[32], uint8_t &len) {
    uint8_t out[48];
    size_t n = 0, bits = 0;
    uint32_t acc = 0;
    for (const char *p = in; *p; p++) {
        if (*p == '=') break;
        int v = b64Val(*p);
        if (v < 0) return false;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n >= sizeof out) return false;
            out[n++] = (uint8_t)(acc >> bits);
        }
    }
    if (n != 0 && n != 1 && n != 16 && n != 32) return false;
    memcpy(key, out, n);
    len = (uint8_t)n;
    return true;
}

void formatKeyBase64(const uint8_t *key, uint8_t len, char *out, size_t cap) {
    size_t o = 0;
    for (uint8_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)key[i] << 16;
        if (i + 1 < len) v |= (uint32_t)key[i + 1] << 8;
        if (i + 2 < len) v |= key[i + 2];
        char quad[4] = {kB64[(v >> 18) & 63], kB64[(v >> 12) & 63],
                        i + 1 < len ? kB64[(v >> 6) & 63] : '=',
                        i + 2 < len ? kB64[v & 63] : '='};
        for (int k = 0; k < 4 && o + 1 < cap; k++) out[o++] = quad[k];
    }
    if (cap) out[o < cap ? o : cap - 1] = 0;
}
