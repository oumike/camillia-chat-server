#pragma once
// The `cfg` serial CLI, compatible with camillia-mt's (handleCfgCommand() in its
// src/main_lvgl.cpp): the web flasher's Customize dialog stages settings, a
// channel plan and a boot logo, then commits them. Community presets are applied
// through it too. Pure C++ (no Arduino); the device supplies CfgHooks and carries
// out a commit (save, clear history, restart).
#include <stddef.h>
#include <stdint.h>
#include <vector>
#include "settings.h"

constexpr size_t CFG_LOGO_MAX = 8192;

struct CfgHooks {
    int (*presetFromName)(const char *name);    // -1 if unknown or unusable on this radio
    const char *(*presetName)(uint8_t preset);  // Meshtastic channel name ("LongFast")
    bool (*regionKnown)(const char *code);
    bool (*validate)(const Settings &s, char *err, size_t cap);
    const char *presetList;                     // comma-separated, for caps
    bool hasLogo;                               // false on the OLED and e-paper builds
    uint16_t logoW, logoH;
};

enum class CfgResult { Reply, Commit };   // Commit: the caller applies staged() and the logo

class CfgCli {
public:
    explicit CfgCli(const CfgHooks &h) : _h(h) { reset(); }
    // `line` is everything after "cfg ". Writes one reply line (no newline) to out.
    CfgResult handle(const char *line, const Settings &live, char *out, size_t cap);

    const Settings &staged() const { return _s; }
    bool logoStaged() const { return _logoState == LogoReady; }
    bool logoClear() const { return _logoState == LogoClear; }
    const uint8_t *logoData() const { return _logo.data(); }
    size_t logoLen() const { return _logo.size(); }
    void reset();   // drop everything staged

private:
    struct Chan {
        char name[12];
        uint8_t key[32];
        uint8_t keyLen;
    };
    enum LogoState { LogoNone, LogoReceiving, LogoReady, LogoClear };

    CfgResult set(const char *field, const char *value, char *out, size_t cap);
    CfgResult setChannel(const char *args, char *out, size_t cap);
    CfgResult setMqtt(const char *args, char *out, size_t cap);
    CfgResult logo(const char *args, char *out, size_t cap);
    CfgResult commit(char *out, size_t cap);
    void stage(const Settings &live);

    CfgHooks _h;
    Settings _s;
    bool _staged = false;
    bool _channelsStaged = false;
    int _chanCount = 0;
    Chan _ch[SETTINGS_MAX_CHANNELS];
    LogoState _logoState = LogoNone;
    size_t _logoExpect = 0;
    std::vector<uint8_t> _logo;
};

// Percent-decodes `in` byte for byte. False on a bad escape or if it does not fit.
bool cfgPercentDecode(const char *in, char *out, size_t cap);
// CRC-32 (zlib), as the flasher's crc32().
uint32_t cfgCrc32(const uint8_t *p, size_t n);
