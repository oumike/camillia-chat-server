#pragma once
// Web config (spec §5.5): one form, served on the AP and, when joined, the STA network.
#include <Arduino.h>
#include <functional>
#include "settings.h"

// onSaved runs after a validated save has been written to NVS.
// Calls emit once per stored message on `slot`, newest first, with one JSON object.
using MessageLister = std::function<void(int slot, const std::function<void(const char *)> &emit)>;

void webBegin(Settings *settings, std::function<void()> onSaved,
              std::function<String()> statusJson, MessageLister listMessages);
void webLoop();
