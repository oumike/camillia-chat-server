#pragma once
// LittleFS persistence of channel stores (spec §5.3).
#include <stdint.h>
#include "channel_store.h"
#include "settings.h"

bool persistBegin();
// LittleFS bytes used / total (0/0 if not mounted).
void persistUsage(size_t &used, size_t &total);
// Size of a channel's saved file in bytes (0 if none).
size_t persistFileSize(int slot);
// Loads each configured channel; a channel whose name/key changed since it was
// saved (or whose file is corrupt) starts empty with a new epoch.
void persistLoadAll(ChannelStore *stores, const Settings &s);
// Saves a store when dirty and (60 s since its last save or >= 20 new messages).
// Writes one store now (after a clear), regardless of the save schedule.
void persistSaveNow(ChannelStore &store, int slot);
void persistMaybeSave(ChannelStore *stores, int n, uint32_t nowMs);
