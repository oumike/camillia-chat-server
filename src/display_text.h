#pragma once
// Text helpers for the expansion display (spec §5.3). Pure C++, no Arduino.
#include <stddef.h>
#include <stdint.h>

void formatAge(uint32_t sec, char *out, size_t cap);      // "now" <60s, "3m", "2h", "4d", "?" for csp::AGE_UNKNOWN
void formatUptime(uint32_t sec, char *out, size_t cap);   // "45s", "12m", "5h 3m", "3d 4h"
void oneLine(const char *in, char *out, size_t cap);      // \r \n \t -> space; cut with "…" (UTF-8) to fit cap
