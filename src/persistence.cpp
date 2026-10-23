#include "persistence.h"
#include <Arduino.h>
#include <LittleFS.h>
#include "cs_proto.h"

static uint8_t *s_buf = nullptr;
static size_t   s_bufCap = 0;
static uint32_t s_lastSaveMs[CS_MAX_CHANNELS] = {0};
static bool     s_ok = false;
static size_t   s_psramBudget = 0;

// PSRAM left for everything allocated after the stores (OTA, web, display).
static constexpr size_t kPsramReserve = 128 * 1024;

static String path(int i, const char *ext) { return String("/cs/ch") + i + ext; }
static void save(ChannelStore &st, int i);

void persistNoteFreePsram() { s_psramBudget = ESP.getFreePsram(); }

bool persistFits(const Settings &s, char *err, size_t cap) {
    const int n = s.chanCount, msgs = s.msgsPerChannel;
    const size_t file = ChannelStore::maxSerializedSize(msgs);
    // Rings, the save buffer, and the web UI's message-list buffer.
    const size_t ram = (size_t)(n + 1) * ChannelStore::ramBytes(msgs) + file + kPsramReserve;
    if (s_psramBudget && ram > s_psramBudget) {
        snprintf(err, cap, "%d channels x %d messages need %u KB of PSRAM; %u KB available", n, msgs,
                 (unsigned)(ram / 1024), (unsigned)(s_psramBudget / 1024));
        return false;
    }
    // Every channel full, plus one .tmp while saving; LittleFS keeps ~10% for metadata.
    const size_t flash = (size_t)(n + 1) * file, total = s_ok ? LittleFS.totalBytes() : 0;
    if (total && flash > total / 10 * 9) {
        snprintf(err, cap, "%d channels x %d messages need up to %u KB of flash; %u KB available", n, msgs,
                 (unsigned)(flash / 1024), (unsigned)(total / 10 * 9 / 1024));
        return false;
    }
    return true;
}

bool persistBegin(int capacity) {
    s_ok = LittleFS.begin(true, "/littlefs", 10, "littlefs");
    if (s_ok && !LittleFS.exists("/cs")) LittleFS.mkdir("/cs");
    s_bufCap = ChannelStore::maxSerializedSize(capacity);
    s_buf = (uint8_t *)ps_malloc(s_bufCap);
    Serial.printf("[cs] littlefs %s, %u/%u KB used\n", s_ok ? "ok" : "FAILED",
                  s_ok ? (unsigned)(LittleFS.usedBytes() / 1024) : 0,
                  s_ok ? (unsigned)(LittleFS.totalBytes() / 1024) : 0);
    return s_ok && s_buf;
}

void persistUsage(size_t &used, size_t &total) {
    used = s_ok ? LittleFS.usedBytes() : 0;
    total = s_ok ? LittleFS.totalBytes() : 0;
}

size_t persistFileSize(int slot) {
    if (!s_ok) return 0;
    File f = LittleFS.open(path(slot, ".bin"), "r");
    return f ? f.size() : 0;
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
            // Replace the old channel's file with the empty store BEFORE recording
            // the new channel ID, so a restart can never pair them.
            save(stores[i], i);
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

void persistSaveNow(ChannelStore &store, int slot) {
    if (s_ok && s_buf) save(store, slot);
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
