#include "display_text.h"
#include "cs_proto.h"
#include <stdio.h>
#include <string.h>

void formatAge(uint32_t sec, char *out, size_t cap) {
    if (!cap) return;
    if (sec == csp::AGE_UNKNOWN)  snprintf(out, cap, "?");
    else if (sec < 60)            snprintf(out, cap, "now");
    else if (sec < 3600)          snprintf(out, cap, "%um", (unsigned)(sec / 60));
    else if (sec < 86400)         snprintf(out, cap, "%uh", (unsigned)(sec / 3600));
    else                          snprintf(out, cap, "%ud", (unsigned)(sec / 86400));
}

void formatUptime(uint32_t sec, char *out, size_t cap) {
    if (!cap) return;
    if (sec < 60)         snprintf(out, cap, "%us", (unsigned)sec);
    else if (sec < 3600)  snprintf(out, cap, "%um", (unsigned)(sec / 60));
    else if (sec < 86400) snprintf(out, cap, "%uh %um", (unsigned)(sec / 3600), (unsigned)(sec % 3600 / 60));
    else                  snprintf(out, cap, "%ud %uh", (unsigned)(sec / 86400), (unsigned)(sec % 86400 / 3600));
}

namespace {
// Longest prefix of at most `max` bytes that does not end mid-codepoint.
size_t utf8Prefix(const char *s, size_t len, size_t max) {
    if (len <= max) return len;
    size_t n = max;
    while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
    return n;
}
char flat(char c) { return (c == '\r' || c == '\n' || c == '\t') ? ' ' : c; }
}  // namespace

void oneLine(const char *in, char *out, size_t cap) {
    if (!cap) return;
    size_t len = strlen(in);
    size_t room = cap - 1;
    size_t n;
    bool ellipsis = false;
    if (len <= room) {
        n = len;
    } else if (room >= 3) {
        n = utf8Prefix(in, len, room - 3);
        ellipsis = true;
    } else {
        n = utf8Prefix(in, len, room);
    }
    for (size_t i = 0; i < n; i++) out[i] = flat(in[i]);
    if (ellipsis) { memcpy(out + n, "\xE2\x80\xA6", 3); n += 3; }
    out[n] = 0;
}
