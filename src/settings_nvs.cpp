// NVS persistence for Settings: one blob plus a layout version.
#include <Preferences.h>
#include "settings.h"

static const uint8_t kSettingsVersion = 5;

bool settingsLoad(Settings &s) {
    Preferences p;
    if (!p.begin("cs", true)) return false;
    uint8_t buf[sizeof(Settings)];
    size_t len = p.getBytesLength("blob");
    bool ok = len > 0 && len <= sizeof buf &&
              p.getBytes("blob", buf, len) == len &&
              settingsFromBlob(buf, len, p.getUChar("ver", 0), s);
    p.end();
    return ok;
}

void settingsSave(const Settings &s) {
    Preferences p;
    if (!p.begin("cs", false)) return;
    p.putBytes("blob", &s, sizeof(Settings));
    p.putUChar("ver", kSettingsVersion);
    p.end();
}
