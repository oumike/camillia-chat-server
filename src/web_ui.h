#pragma once
// Web config (spec §5.5): one form, served on the AP and, when joined, the STA network.
#include <Arduino.h>
#include <functional>
#include "settings.h"

// onSaved runs after a validated save has been written to NVS.
void webBegin(Settings *settings, std::function<void()> onSaved,
              std::function<String()> statusJson);
void webLoop();
