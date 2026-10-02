#include "persistence.h"
#include <Arduino.h>
#include <LittleFS.h>
#include "cs_proto.h"

static uint8_t *s_buf = nullptr;
static size_t   s_bufCap = 0;
static uint32_t s_lastSaveMs[CS_MAX_CHANNELS] = {0};
static bool     s_ok = false;

static String path(int i, const char *ext) { return String("/cs/ch") + i + ext; }

bool persistBegin() {
    s_ok = LittleFS.begin(true, "/littlefs", 10, "littlefs");
    if (s_ok && !LittleFS.exists("/cs")) LittleFS.mkdir("/cs");
    s_bufCap = ChannelStore::maxSerializedSize();
    s_buf = (uint8_t *)ps_malloc(s_bufCap);
    Serial.printf("[cs] littlefs %s, %u/%u KB used\n", s_ok ? "ok" : "FAILED",
                  s_ok ? (unsigned)(LittleFS.usedBytes() / 1024) : 0,
                  s_ok ? (unsigned)(LittleFS.totalBytes() / 1024) : 0);
    return s_ok && s_buf;
}

void persistLoadAll(ChannelStore *stores, const Settings &s) {
    for (int i = 0; i < s.chanCount; i++) {
        uint32_t id = csp::channelId(s.ch[i].name, s.ch[i].key, s.ch[i].keyLen);
        bool loaded = false;
        File meta = s_ok ? LittleFS.open(path(i, ".meta"), "r") : File();
        uint32_t savedId = 0;
        if (meta && meta.read((uint8_t *)&savedId, 4) == 4 && savedId == id) {
            File f = LittleFS.open(path(i, ".bin"), "r");
            if (f) {
                size_t n = f.read(s_buf, s_bufCap);
                loaded = stores[i].deserialize(s_buf, n);
            }
        }
        if (!loaded) stores[i].reset(esp_random());
        Serial.printf("[cs] channel %d \"%s\": %s, %d messages, epoch %08x\n", i, s.ch[i].name,
                      loaded ? "loaded" : "new", stores[i].count(), (unsigned)stores[i].epoch());
        if (!loaded && s_ok) {
            File m = LittleFS.open(path(i, ".meta"), "w");
            if (m) m.write((const uint8_t *)&id, 4);
        }
    }
}

static void save(ChannelStore &st, int i) {
    size_t n = st.serialize(s_buf, s_bufCap);
    if (!n) return;
    String tmp = path(i, ".tmp"), dst = path(i, ".bin");
    File f = LittleFS.open(tmp, "w");
    if (!f) return;
    bool ok = f.write(s_buf, n) == n;
    f.close();
    if (!ok) return;
    LittleFS.remove(dst);
    if (LittleFS.rename(tmp, dst)) st.markSaved();
}

void persistMaybeSave(ChannelStore *stores, int n, uint32_t nowMs) {
    if (!s_ok || !s_buf) return;
    for (int i = 0; i < n; i++) {
        ChannelStore &st = stores[i];
        if (!st.dirty()) { s_lastSaveMs[i] = nowMs; continue; }
        if (st.addsSinceSave() >= 20 || nowMs - s_lastSaveMs[i] >= 60000) {
            save(st, i);
            s_lastSaveMs[i] = nowMs;
        }
    }
}
