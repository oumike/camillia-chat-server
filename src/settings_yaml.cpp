#include "settings_yaml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

std::string quote(const char *v) {
    std::string o = "\"";
    for (const char *p = v; *p; p++) {
        if (*p == '"' || *p == '\\') o += '\\';
        o += *p;
    }
    return o + "\"";
}

void kv(std::string &y, const char *indent, const char *key, const std::string &value) {
    y += indent; y += key; y += ": "; y += value; y += "\n";
}

std::string num(long v) { return std::to_string(v); }

// One parsed "key: value" line.
struct Line {
    int indent = 0;
    bool listItem = false;   // starts with "- "
    std::string key, value;
    bool hasValue = false;
};

bool parseValue(const char *p, std::string &out, char *err, size_t cap) {
    out.clear();
    while (*p == ' ') p++;
    if (*p == '"') {
        for (p++; *p && *p != '"'; p++) {
            if (*p == '\\' && p[1]) p++;
            out += *p;
        }
        if (*p != '"') { snprintf(err, cap, "Unterminated quoted value"); return false; }
        return true;
    }
    const char *end = p;
    while (*end && !(*end == '#' && (end == p || end[-1] == ' '))) end++;
    while (end > p && (end[-1] == ' ' || end[-1] == '\r' || end[-1] == '\t')) end--;
    out.assign(p, (size_t)(end - p));
    return true;
}

bool parseLine(const std::string &raw, Line &l, char *err, size_t cap) {
    const char *p = raw.c_str();
    while (*p == ' ') { l.indent++; p++; }
    if (p[0] == '-' && p[1] == ' ') { l.listItem = true; p += 2; l.indent += 2; while (*p == ' ') p++; }
    const char *colon = strchr(p, ':');
    if (!colon) { snprintf(err, cap, "Expected \"key: value\": %.40s", raw.c_str()); return false; }
    l.key.assign(p, (size_t)(colon - p));
    while (!l.key.empty() && l.key.back() == ' ') l.key.pop_back();
    if (!parseValue(colon + 1, l.value, err, cap)) return false;
    const char *rest = colon + 1;
    while (*rest == ' ' || *rest == '\r') rest++;
    l.hasValue = *rest && *rest != '#';
    return true;
}

bool toLong(const std::string &v, long lo, long hi, long &out) {
    if (v.empty()) return false;
    char *end;
    long n = strtol(v.c_str(), &end, 10);
    if (*end || n < lo || n > hi) return false;
    out = n;
    return true;
}

void setStr(char *dst, size_t cap, const std::string &v) {
    memset(dst, 0, cap);
    strncpy(dst, v.c_str(), cap - 1);
}

}  // namespace

std::string settingsToYaml(const Settings &s, const YamlPresetMap &presets) {
    std::string y = "# camillia chat server config\n"
                    "# Contains channel keys and passwords. Keep it private.\n";
    y += "identity:\n";
    kv(y, "  ", "longName", quote(s.longName));
    kv(y, "  ", "shortName", quote(s.shortName));
    y += "radio:\n";
    kv(y, "  ", "region", quote(s.region));
    const char *pn = presets.name ? presets.name(s.modemPreset) : nullptr;
    kv(y, "  ", "preset", pn ? std::string(pn) : num(s.modemPreset));
    kv(y, "  ", "slot", num(s.freqSlot));
    y += "channels:\n";
    for (int i = 0; i < s.chanCount; i++) {
        char key[48];
        formatKeyBase64(s.ch[i].key, s.ch[i].keyLen, key, sizeof key);
        kv(y, "  - ", "name", quote(s.ch[i].name));
        kv(y, "    ", "key", quote(key));
    }
    y += "replies:\n";
    kv(y, "  ", "batchSize", num(s.batchSize));
    kv(y, "  ", "packetGapMs", num(s.packetGapMs));
    kv(y, "  ", "maxHops", num(s.maxHops));
    y += "mqtt:\n";
    kv(y, "  ", "enabled", s.mqttEnabled ? "true" : "false");
    kv(y, "  ", "host", quote(s.mqttHost));
    kv(y, "  ", "port", num(s.mqttPort));
    kv(y, "  ", "user", quote(s.mqttUser));
    kv(y, "  ", "password", quote(s.mqttPass));
    kv(y, "  ", "root", quote(s.mqttRoot));
    y += "wifi:\n";
    kv(y, "  ", "ssid", quote(s.staSsid));
    kv(y, "  ", "password", quote(s.staPass));
    kv(y, "  ", "timezone", quote(s.tz));
    return y;
}

bool settingsFromYaml(const char *text, Settings &out, const YamlPresetMap &presets,
                      char *err, size_t cap) {
    Settings s = out;
    std::string section;
    int chan = -1;            // index of the channel being filled, in a channels section
    bool sawChannels = false;
    long n;

    auto bad = [&](const char *what) {
        snprintf(err, cap, "%s.%s: invalid %s", section.c_str(), what, what);
        return false;
    };

    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        std::string raw = eol ? std::string(p, (size_t)(eol - p)) : std::string(p);
        p = eol ? eol + 1 : p + raw.size();

        size_t first = raw.find_first_not_of(" \t\r");
        if (first == std::string::npos || raw[first] == '#') continue;

        Line l;
        if (!parseLine(raw, l, err, cap)) return false;

        if (l.indent == 0) {
            section = l.key;
            chan = -1;
            if (section == "channels" && !sawChannels) {
                sawChannels = true;
                s.chanCount = 0;
                memset(s.ch, 0, sizeof(s.ch));
            }
            continue;
        }
        const std::string &k = l.key, &v = l.value;

        if (section == "identity") {
            if (k == "longName") setStr(s.longName, sizeof s.longName, v);
            else if (k == "shortName") setStr(s.shortName, sizeof s.shortName, v);
        } else if (section == "radio") {
            if (k == "region") setStr(s.region, sizeof s.region, v);
            else if (k == "preset") {
                int idx = presets.fromName ? presets.fromName(v.c_str()) : -1;
                if (idx < 0 && toLong(v, 0, 255, n)) idx = (int)n;
                if (idx < 0) { snprintf(err, cap, "radio.preset: unknown preset \"%s\"", v.c_str()); return false; }
                s.modemPreset = (uint8_t)idx;
            } else if (k == "slot") {
                if (!toLong(v, 0, 255, n)) return bad("slot");
                s.freqSlot = (uint8_t)n;
            }
        } else if (section == "channels") {
            if (l.listItem) {
                if (s.chanCount >= 3) { snprintf(err, cap, "channels: at most 3 channels"); return false; }
                chan = s.chanCount++;
            }
            if (chan < 0) { snprintf(err, cap, "channels: each channel starts with \"- name:\""); return false; }
            if (k == "name") setStr(s.ch[chan].name, sizeof s.ch[chan].name, v);
            else if (k == "key") {
                memset(s.ch[chan].key, 0, sizeof s.ch[chan].key);
                if (!parseKeyBase64(v.c_str(), s.ch[chan].key, s.ch[chan].keyLen)) {
                    snprintf(err, cap, "channels: key for \"%s\" is not a valid base64 key", s.ch[chan].name);
                    return false;
                }
            }
        } else if (section == "replies") {
            if (k == "batchSize") { if (!toLong(v, 0, 255, n)) return bad("batchSize"); s.batchSize = (uint8_t)n; }
            else if (k == "packetGapMs") { if (!toLong(v, 0, 65535, n)) return bad("packetGapMs"); s.packetGapMs = (uint16_t)n; }
            else if (k == "maxHops") { if (!toLong(v, 0, 255, n)) return bad("maxHops"); s.maxHops = (uint8_t)n; }
        } else if (section == "mqtt") {
            if (k == "enabled") {
                if (v != "true" && v != "false") return bad("enabled");
                s.mqttEnabled = v == "true";
            } else if (k == "host") setStr(s.mqttHost, sizeof s.mqttHost, v);
            else if (k == "port") { if (!toLong(v, 1, 65535, n)) return bad("port"); s.mqttPort = (uint16_t)n; }
            else if (k == "user") setStr(s.mqttUser, sizeof s.mqttUser, v);
            else if (k == "password") setStr(s.mqttPass, sizeof s.mqttPass, v);
            else if (k == "root") setStr(s.mqttRoot, sizeof s.mqttRoot, v);
        } else if (section == "wifi") {
            if (k == "ssid") setStr(s.staSsid, sizeof s.staSsid, v);
            else if (k == "password") setStr(s.staPass, sizeof s.staPass, v);
            else if (k == "timezone") setStr(s.tz, sizeof s.tz, v);
        }
    }

    if (!settingsValidate(s, err, cap)) return false;
    out = s;
    return true;
}
