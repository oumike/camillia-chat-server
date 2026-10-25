#pragma once
// Web config (spec §5.5): one form, served on the AP and, when joined, the STA network.
#include <Arduino.h>
#include <functional>
#include "settings.h"

// onSaved runs after a validated save has been written to NVS.
// Calls emit once per stored message on `slot`, newest first, with one JSON object.
using MessageLister = std::function<void(int slot, const std::function<void(const char *)> &emit)>;

// Clears stored messages on one slot, or every slot when slot is -1.
using MessageClearer = std::function<void(int slot)>;

void webBegin(Settings *settings, std::function<void()> onSaved,
              std::function<String()> statusJson, MessageLister listMessages,
              MessageClearer clearMessages, std::function<String()> storageJson);
// Handler for the "Send announcement" button; returns false if it could not be queued.
void webSetAnnounce(std::function<bool()> announce);
// Community presets for the Radio card: apply(idx, err, cap) runs one through
// the cfg CLI (false with the reason), problem(idx) says why one cannot be used.
void webSetPresets(std::function<bool(int, char *, size_t)> apply, std::function<const char *(int)> problem);
void webLoop();
