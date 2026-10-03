// NVS persistence for Settings: one blob plus a layout version.
#include <Preferences.h>
#include "settings.h"

static const uint8_t kSettingsVersion = 4;

bool settingsLoad(Settings &s) {
    Preferences p;
    if (!p.begin("cs", true)) return false;
    bool ok = p.getUChar("ver", 0) == kSettingsVersion &&
              p.getBytesLength("blob") == sizeof(Settings) &&
              p.getBytes("blob", &s, sizeof(Settings)) == sizeof(Settings);
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
