#include "ota_version.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

bool otaIsPrerelease(const char *tag) { return tag && strchr(tag, '-') != nullptr; }

bool otaVersionKnown(const char *v) {
    for (; v && *v; v++)
        if (isdigit((unsigned char)*v)) return true;
    return false;
}

// Next run of digits in s[i..end), or -1 when there is none.
static long nextNumber(const char *s, size_t &i, size_t end) {
    while (i < end && !isdigit((unsigned char)s[i])) i++;
    if (i >= end) return -1;
    long v = 0;
    while (i < end && isdigit((unsigned char)s[i])) v = v * 10 + (s[i++] - '0');
    return v;
}

static int compareNumbers(const char *a, size_t ai, size_t aEnd, const char *b, size_t bi, size_t bEnd) {
    for (;;) {
        long va = nextNumber(a, ai, aEnd), vb = nextNumber(b, bi, bEnd);
        if (va < 0 && vb < 0) return 0;
        if (va < 0) va = 0;
        if (vb < 0) vb = 0;
        if (va != vb) return va < vb ? -1 : 1;
    }
}

int otaCompareVersions(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    const char *aDash = strchr(a, '-'), *bDash = strchr(b, '-');
    const size_t aEnd = aDash ? (size_t)(aDash - a) : strlen(a);
    const size_t bEnd = bDash ? (size_t)(bDash - b) : strlen(b);
    int c = compareNumbers(a, 0, aEnd, b, 0, bEnd);
    if (c) return c;
    if (aDash && !bDash) return -1;
    if (!aDash && bDash) return 1;
    if (!aDash) return 0;
    return compareNumbers(a, aEnd + 1, strlen(a), b, bEnd + 1, strlen(b));
}

bool otaUpdateAvailable(const char *running, const char *latestTag) {
    if (!latestTag || !latestTag[0] || otaIsPrerelease(latestTag)) return false;
    return otaCompareVersions(running, latestTag) < 0;
}

bool otaFindTagName(const char *json, char *out, size_t cap) {
    if (!json || !out || cap == 0) return false;
    const char *p = strstr(json, "\"tag_name\"");
    if (!p) return false;
    p += 10;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p++ != ':') return false;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p++ != '"') return false;
    size_t n = 0;
    for (; *p && *p != '"'; p++) {
        // A tag goes into a URL path: only the characters release.sh produces.
        if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-' && *p != '_') return false;
        if (n + 1 >= cap) return false;
        out[n++] = *p;
    }
    if (*p != '"' || n == 0) return false;
    out[n] = 0;
    return true;
}

bool otaAssetName(const char *env, const char *tag, char *out, size_t cap) {
    if (!env || !env[0] || !tag || !tag[0]) return false;
    int n = snprintf(out, cap, "camillia-chat-server-%s-%s-ota.bin", env, tag);
    return n > 0 && (size_t)n < cap;
}
