#pragma once
// YAML export/import of Settings for the web config. Pure C++ (no Arduino).
// The file carries channel keys and passwords: it is a backup, not a public file.
#include <stddef.h>
#include <stdint.h>
#include <string>
#include "settings.h"

// Preset index <-> Meshtastic channel name ("LongFast"), supplied by the caller
// so this file does not depend on the radio tables.
struct YamlPresetMap {
    const char *(*name)(uint8_t preset);   // nullptr if unknown
    int (*fromName)(const char *name);     // -1 if unknown
};

std::string settingsToYaml(const Settings &s, const YamlPresetMap &presets);

// Starts from `s`, applies every key present, validates. On failure returns
// false with a reason in err and leaves `s` untouched. A `channels:` section
// replaces the whole channel list; keys not mentioned keep their values.
bool settingsFromYaml(const char *text, Settings &s, const YamlPresetMap &presets,
                      char *err, size_t errCap);
