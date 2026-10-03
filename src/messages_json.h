#pragma once
// JSON for the web "Messages" tab. Pure C++, no Arduino.
#include <stddef.h>
#include <stdint.h>
#include "channel_store.h"

// One stored message as a JSON object. ageSec 0xFFFFFFFF is written as null.
// Returns the length written (NUL-terminated), or 0 if out is too small.
size_t messageJson(const StoredMsg &m, uint32_t ageSec, char *out, size_t cap);
