#include "cfg_cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace {

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Splits off the first space-separated word of *p into word, advancing *p past it.
void nextWord(const char *&p, char *word, size_t cap) {
    while (*p == ' ') p++;
    size_t n = 0;
    while (*p && *p != ' ') {
        if (n + 1 < cap) word[n++] = *p;
        p++;
    }
    word[n] = 0;
    while (*p == ' ') p++;
}

bool parseUint(const char *s, unsigned long max, unsigned long &out) {
    if (!*s) return false;
    char *end;
    out = strtoul(s, &end, 10);
    return *end == 0 && out <= max;
}

// key=value from a channel line; empty if the key is absent.
bool argValue(const char *args, const char *key, char *out, size_t cap) {
    const size_t klen = strlen(key);
    for (const char *p = args; *p;) {
        while (*p == ' ') p++;
        const char *end = strchr(p, ' ');
        if (!end) end = p + strlen(p);
        if ((size_t)(end - p) > klen && !strncmp(p, key, klen) && p[klen] == '=') {
            size_t n = (size_t)(end - p) - klen - 1;
            if (n >= cap) return false;
            memcpy(out, p + klen + 1, n);
            out[n] = 0;
            return true;
        }
        p = end;
    }
    out[0] = 0;
    return true;
}

CfgResult reply(char *out, size_t cap, const char *fmt, const char *a = "", const char *b = "") {
    snprintf(out, cap, fmt, a, b);
    return CfgResult::Reply;
}

}   // namespace

bool cfgPercentDecode(const char *in, char *out, size_t cap) {
    size_t n = 0;
    for (const char *p = in; *p; p++) {
        char c = *p;
        if (c == '%') {
            int hi = hexVal(p[1]), lo = hi < 0 ? -1 : hexVal(p[2]);
            if (hi < 0 || lo < 0) return false;
            c = (char)(hi << 4 | lo);
            p += 2;
        }
        if (n + 1 >= cap) return false;
        out[n++] = c;
    }
    if (!cap) return false;
    out[n] = 0;
    return true;
}

uint32_t cfgCrc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return c ^ 0xFFFFFFFFu;
}

void CfgCli::reset() {
    _staged = false;
    _channelsStaged = false;
    _chanCount = 0;
    _logoState = LogoNone;
    _logoExpect = 0;
    _logo.clear();
    _logo.shrink_to_fit();
}

void CfgCli::stage(const Settings &live) {
    if (_staged) return;
    _s = live;
    _staged = true;
}

CfgResult CfgCli::handle(const char *line, const Settings &live, char *out, size_t cap) {
    const char *p = line;
    char cmd[16];
    nextWord(p, cmd, sizeof cmd);

    if (!strcmp(cmd, "caps")) {
        snprintf(out, cap,
                 "[cfg] caps v=1 product=cs firstboot=0 keys=preset,slot,channels,setup,mqtt%s channels=%d "
                 "presets=%s",
                 _h.hasLogo ? ",logo" : "", SETTINGS_MAX_CHANNELS, _h.presetList);
        if (_h.hasLogo) {
            size_t n = strlen(out);
            snprintf(out + n, cap - n, " logo=%ux%u logobg=100C29 logomax=%u", _h.logoW, _h.logoH,
                     (unsigned)CFG_LOGO_MAX);
        }
        return CfgResult::Reply;
    }
    if (!strcmp(cmd, "get")) {
        char what[16];
        nextWord(p, what, sizeof what);
        if (strcmp(what, "preset")) return reply(out, cap, "[cfg] err %s: unknown", what);
        const char *name = _h.presetName((_staged ? _s : live).modemPreset);
        return reply(out, cap, "[cfg] preset=%s", name ? name : "?");
    }
    if (!strcmp(cmd, "set")) {
        char field[16];
        nextWord(p, field, sizeof field);
        if (!strcmp(field, "channel")) { stage(live); return setChannel(p, out, cap); }
        if (!strcmp(field, "mqtt")) { stage(live); return setMqtt(p, out, cap); }
        if (!strcmp(field, "role")) return reply(out, cap, "[cfg] err role: not supported");
        stage(live);
        return set(field, p, out, cap);
    }
    if (!strcmp(cmd, "channels")) {
        char what[16];
        nextWord(p, what, sizeof what);
        if (strcmp(what, "clear")) return reply(out, cap, "[cfg] err channels: unknown");
        stage(live);
        _channelsStaged = true;
        _chanCount = 0;
        snprintf(out, cap, "[cfg] ok channels cleared slots=%d", SETTINGS_MAX_CHANNELS);
        return CfgResult::Reply;
    }
    if (!strcmp(cmd, "logo")) return logo(p, out, cap);
    if (!strcmp(cmd, "commit")) return commit(out, cap);
    if (!strcmp(cmd, "abort")) {
        reset();
        return reply(out, cap, "[cfg] ok aborted");
    }
    return reply(out, cap, "[cfg] err %s: unknown", cmd);
}

CfgResult CfgCli::set(const char *field, const char *value, char *out, size_t cap) {
    if (!strcmp(field, "preset")) {
        int i = _h.presetFromName(value);
        if (i < 0) return reply(out, cap, "[cfg] err preset: unknown or not supported by this radio");
        _s.modemPreset = (uint8_t)i;
        return reply(out, cap, "[cfg] ok preset=%s staged", _h.presetName((uint8_t)i));
    }
    if (!strcmp(field, "slot")) {
        unsigned long n;
        if (!parseUint(value, 255, n)) return reply(out, cap, "[cfg] err slot: must be 0-255");
        _s.freqSlot = (uint8_t)n;
        snprintf(out, cap, "[cfg] ok slot=%lu staged", n);
        return CfgResult::Reply;
    }
    if (!strcmp(field, "region")) {
        if (strlen(value) >= sizeof _s.region || !_h.regionKnown(value))
            return reply(out, cap, "[cfg] err region: unknown region");
        strcpy(_s.region, value);
        return reply(out, cap, "[cfg] ok region=%s staged", value);
    }
    struct Text { const char *name; char *dst; size_t size; bool required; };
    const Text texts[] = {
        {"longname", _s.longName, sizeof _s.longName, true},
        {"shortname", _s.shortName, sizeof _s.shortName, true},
        {"ssid", _s.staSsid, sizeof _s.staSsid, false},
        {"wifipass", _s.staPass, sizeof _s.staPass, false},
    };
    for (const Text &t : texts) {
        if (strcmp(field, t.name)) continue;
        char buf[128];
        if (!cfgPercentDecode(value, buf, sizeof buf)) {
            // Tell a bad escape from a value that is merely too long.
            bool badEscape = false;
            for (const char *q = value; *q; q++)
                if (*q == '%' && (hexVal(q[1]) < 0 || hexVal(q[2]) < 0)) { badEscape = true; break; }
                else if (*q == '%') q += 2;
            return reply(out, cap, badEscape ? "[cfg] err %s: bad encoding" : "[cfg] err %s: too long", t.name);
        }
        if (strlen(buf) >= t.size) return reply(out, cap, "[cfg] err %s: too long", t.name);
        if (t.required && !buf[0]) return reply(out, cap, "[cfg] err %s: required", t.name);
        strcpy(t.dst, buf);
        return reply(out, cap, "[cfg] ok %s staged", t.name);
    }
    return reply(out, cap, "[cfg] err %s: unknown", field);
}

CfgResult CfgCli::setChannel(const char *args, char *out, size_t cap) {
    const char *p = args;
    char idx[8];
    nextWord(p, idx, sizeof idx);
    unsigned long i;
    if (!parseUint(idx, SETTINGS_MAX_CHANNELS - 1, i))
        return reply(out, cap, "[cfg] err channel %s: index must be 0-19", idx);
    if (!_channelsStaged) {   // MT sends channels clear first; accept a plan without it
        _channelsStaged = true;
        _chanCount = 0;
    }
    if ((int)i > _chanCount) return reply(out, cap, "[cfg] err channel %s: channels must be sent in order", idx);

    char raw[96], name[64], psk[64];
    if (!argValue(p, "name", raw, sizeof raw) || !cfgPercentDecode(raw, name, sizeof name))
        return reply(out, cap, "[cfg] err channel %s: bad name", idx);
    if (strlen(name) > 11) return reply(out, cap, "[cfg] err channel %s: name is over 11 bytes", idx);
    if (!name[0] && i != 0) return reply(out, cap, "[cfg] err channel %s: name required", idx);
    if (!argValue(p, "psk", psk, sizeof psk)) return reply(out, cap, "[cfg] err channel %s: bad psk", idx);

    Chan c{};
    strcpy(c.name, name);
    if (!strcmp(psk, "default")) {
        c.key[0] = 0x01;
        c.keyLen = 1;
    } else if (!strcmp(psk, "none") || !psk[0]) {
        c.keyLen = 0;
    } else if (!parseKeyBase64(psk, c.key, c.keyLen)) {
        return reply(out, cap, "[cfg] err channel %s: psk must be default, none or 1/16/32 bytes of base64", idx);
    }
    _ch[i] = c;
    if ((int)i == _chanCount) _chanCount++;
    return reply(out, cap, "[cfg] ok channel %s staged", idx);
}

CfgResult CfgCli::setMqtt(const char *args, char *out, size_t cap) {
    const char *p = args;
    char what[8];
    nextWord(p, what, sizeof what);
    if (!strcmp(what, "on") || !strcmp(what, "off")) {
        _s.mqttEnabled = what[1] == 'n';
        return reply(out, cap, "[cfg] ok mqtt staged");
    }
    if (!strcmp(what, "port")) {
        unsigned long n;
        if (!parseUint(p, 65535, n) || n == 0) return reply(out, cap, "[cfg] err mqtt port: must be 1-65535");
        _s.mqttPort = (uint16_t)n;
        return reply(out, cap, "[cfg] ok mqtt port staged");
    }
    struct Text { const char *name; char *dst; size_t size; };
    const Text texts[] = {
        {"host", _s.mqttHost, sizeof _s.mqttHost},
        {"user", _s.mqttUser, sizeof _s.mqttUser},
        {"pass", _s.mqttPass, sizeof _s.mqttPass},
        {"root", _s.mqttRoot, sizeof _s.mqttRoot},
    };
    for (const Text &t : texts) {
        if (strcmp(what, t.name)) continue;
        char buf[128];
        if (!cfgPercentDecode(p, buf, sizeof buf) || strlen(buf) >= t.size)
            return reply(out, cap, "[cfg] err mqtt %s: bad encoding or too long", t.name);
        strcpy(t.dst, buf);
        return reply(out, cap, "[cfg] ok mqtt %s staged", t.name);
    }
    return reply(out, cap, "[cfg] err mqtt %s: unknown", what);
}

CfgResult CfgCli::logo(const char *args, char *out, size_t cap) {
    if (!_h.hasLogo) return reply(out, cap, "[cfg] err logo: no logo on this display");
    const char *p = args;
    char what[8];
    nextWord(p, what, sizeof what);
    if (!strcmp(what, "begin")) {
        unsigned long n;
        if (!parseUint(p, CFG_LOGO_MAX, n) || n == 0) {
            _logoState = LogoNone;
            return reply(out, cap, "[cfg] err logo: size must be 1-8192 bytes");
        }
        _logo.clear();
        _logo.reserve(n);
        _logoExpect = n;
        _logoState = LogoReceiving;
        return reply(out, cap, "[cfg] ok logo begin");
    }
    if (!strcmp(what, "data")) {
        if (_logoState != LogoReceiving) return reply(out, cap, "[cfg] err logo: data before begin");
        const size_t len = strlen(p);
        if (len % 2 || _logo.size() + len / 2 > _logoExpect) {
            _logoState = LogoNone;
            _logo.clear();
            return reply(out, cap, "[cfg] err logo: data does not fit the announced size");
        }
        for (size_t i = 0; i < len; i += 2) {
            int hi = hexVal(p[i]), lo = hexVal(p[i + 1]);
            if (hi < 0 || lo < 0) {
                _logoState = LogoNone;
                _logo.clear();
                return reply(out, cap, "[cfg] err logo: bad hex");
            }
            _logo.push_back((uint8_t)(hi << 4 | lo));
        }
        snprintf(out, cap, "[cfg] ok logo %u/%u", (unsigned)_logo.size(), (unsigned)_logoExpect);
        return CfgResult::Reply;
    }
    if (!strcmp(what, "end")) {
        if (_logoState != LogoReceiving) return reply(out, cap, "[cfg] err logo: end before begin");
        const uint32_t want = (uint32_t)strtoul(p, nullptr, 16);
        if (_logo.size() != _logoExpect || cfgCrc32(_logo.data(), _logo.size()) != want) {
            _logoState = LogoNone;
            _logo.clear();
            return reply(out, cap, "[cfg] err logo: size or CRC mismatch");
        }
        _logoState = LogoReady;
        return reply(out, cap, "[cfg] ok logo staged");
    }
    if (!strcmp(what, "clear")) {
        _logo.clear();
        _logoState = LogoClear;
        return reply(out, cap, "[cfg] ok logo cleared");
    }
    return reply(out, cap, "[cfg] err logo: unknown");
}

CfgResult CfgCli::commit(char *out, size_t cap) {
    const bool logoPending = _logoState == LogoReady || _logoState == LogoClear;
    if (!_staged && !logoPending) return reply(out, cap, "[cfg] ok nothing");
    if (_staged) {
        Settings n = _s;
        if (_channelsStaged) {
            n.chanCount = (uint8_t)_chanCount;
            memset(n.ch, 0, sizeof n.ch);
            for (int i = 0; i < _chanCount; i++) {
                ChannelCfg &c = n.ch[i];
                const char *name = _ch[i].name;
                // An unnamed primary is named after the preset, as Meshtastic does.
                if (!name[0]) name = _h.presetName(n.modemPreset) ? _h.presetName(n.modemPreset) : "LongFast";
                snprintf(c.name, sizeof c.name, "%s", name);
                memcpy(c.key, _ch[i].key, sizeof c.key);
                c.keyLen = _ch[i].keyLen;
            }
        }
        char err[96] = "";
        if (!_h.validate(n, err, sizeof err)) return reply(out, cap, "[cfg] err commit: %s", err);
        _s = n;
    }
    snprintf(out, cap, "[cfg] ok committed rebooting");
    return CfgResult::Commit;
}
