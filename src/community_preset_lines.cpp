#include "community_preset_lines.h"
#include <stdio.h>
#include <string.h>
#include "community_presets.h"

// Percent-encodes all but plain ASCII, as the flasher's percentEncode().
static void enc(const char *in, char *out, size_t cap) {
    static const char kHex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && n + 4 < cap; p++) {
        const unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || strchr("_.~-", c)) {
            out[n++] = (char)c;
        } else {
            out[n++] = '%';
            out[n++] = kHex[c >> 4];
            out[n++] = kHex[c & 15];
        }
    }
    out[n] = 0;
}

int communityPresetLines(int idx, CfgPresetLine *lines, int max) {
    if (idx < 0 || idx >= kCommunityPresetCount) return 0;
    const CommunityPreset &p = kCommunityPresets[idx];
    int n = 0;
    auto add = [&](const char *fmt, auto... args) {
        if (n < max) snprintf(lines[n].text, sizeof lines[n].text, fmt, args...);
        n++;
    };

    add("set region %s", p.region);
    if (p.preset[0]) {   // an empty preset keeps the server's own radio, slot included
        add("set preset %s", p.preset);
        add("set slot %u", (unsigned)p.slot);
    }
    add("%s", "channels clear");
    for (int i = 0; i < p.channelCount; i++) {
        const CommunityPresetChannel &c = kCommunityPresetChannels[p.firstChannel + i];
        char name[40];
        enc(c.name, name, sizeof name);
        add("set channel %d role=%s name=%s psk=%s", i, i ? "secondary" : "primary", name, c.psk);
    }
    if (p.mqtt.host[0]) {
        char host[200], user[100], pass[200], root[150];
        enc(p.mqtt.host, host, sizeof host);
        enc(p.mqtt.user, user, sizeof user);
        enc(p.mqtt.pass, pass, sizeof pass);
        enc(p.mqtt.root, root, sizeof root);
        add("set mqtt host=%s port=%u user=%s pass=%s root=%s tls=%d enc=%d", host, (unsigned)p.mqtt.port, user, pass,
            root, p.mqtt.tls ? 1 : 0, p.mqtt.encryption ? 1 : 0);
    }
    add("%s", "commit");
    return n <= max ? n : 0;
}
