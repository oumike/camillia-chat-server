#pragma once
// Shim replacing camillia-mt's debug_flags.h: everything goes to Serial.
void debugLogAcks(const char *fmt, ...);
void debugLogMessages(const char *fmt, ...);
void debugLogGps(const char *fmt, ...);
static inline bool debugAcksEnabled()     { return false; }
static inline bool debugMessagesEnabled() { return true; }
static inline bool debugGpsEnabled()      { return false; }
