#include "debug_flags.h"
#include <Arduino.h>
#include <stdarg.h>

static void vlog(const char *fmt, va_list ap) {
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    Serial.print(buf);
}
void debugLogAcks(const char *, ...) {}
void debugLogGps(const char *, ...) {}
void debugLogMessages(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vlog(fmt, ap); va_end(ap);
}
