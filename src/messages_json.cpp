#include "messages_json.h"
#include <stdio.h>

namespace {

struct Out {
    char *p; size_t cap; size_t n = 0; bool ok = true;
    void put(char c) { if (n + 1 >= cap) { ok = false; return; } p[n++] = c; }
    void str(const char *s) { while (*s) put(*s++); }
    void fmt(const char *f, unsigned long v) {
        char b[24];
        snprintf(b, sizeof b, f, v);
        str(b);
    }
};

// JSON string escaping; "</" is written as "<\/" so text can't close a <script>.
void escaped(Out &o, const char *s, size_t len) {
    o.put('"');
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  o.str("\\\""); break;
            case '\\': o.str("\\\\"); break;
            case '\n': o.str("\\n"); break;
            case '\r': o.str("\\r"); break;
            case '\t': o.str("\\t"); break;
            case '/':  o.str(i > 0 && s[i - 1] == '<' ? "\\/" : "/"); break;
            default:
                if (c < 0x20) o.fmt("\\u%04lx", c);
                else o.put((char)c);
        }
    }
    o.put('"');
}

}  // namespace

size_t messageJson(const StoredMsg &m, uint32_t ageSec, char *out, size_t cap) {
    Out o{out, cap};
    o.fmt("{\"seq\":%lu", m.seq);
    o.fmt(",\"from\":\"!%08lx\"", m.from);
    o.fmt(",\"rxUnix\":%lu", m.rxUnix);
    if (ageSec == 0xFFFFFFFF) o.str(",\"ageSec\":null");
    else o.fmt(",\"ageSec\":%lu", ageSec);
    o.str(m.source == SRC_MQTT ? ",\"source\":\"mqtt\"" : ",\"source\":\"lora\"");
    o.str(",\"text\":");
    escaped(o, m.text, m.textLen);
    o.put('}');
    if (!o.ok) return 0;
    out[o.n] = 0;
    return o.n;
}
