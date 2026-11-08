#include "node_persist.h"
#include <Arduino.h>
#include <LittleFS.h>

static constexpr uint32_t kSaveEveryMs = 5 * 60 * 1000;
static constexpr size_t kBufCap =
    NodeNames::BLOB_MAX > PeerKeys::BLOB_MAX ? NodeNames::BLOB_MAX : PeerKeys::BLOB_MAX;
static const char *const kNamesPath = "/cs/nodes.bin";
static const char *const kKeysPath = "/cs/keys.bin";

static uint8_t *s_buf = nullptr;
static uint32_t s_namesRev = 0, s_keysRev = 0;
static uint32_t s_namesAtMs = 0, s_keysAtMs = 0;

static size_t readFile(const char *path) {
    File f = LittleFS.open(path, "r");
    if (!f) return 0;
    size_t len = f.size();
    bool ok = len <= kBufCap && f.read(s_buf, len) == len;
    f.close();
    return ok ? len : 0;
}

static bool writeFile(const char *path, size_t len) {
    String tmp = String(path) + ".tmp";
    File f = LittleFS.open(tmp, "w");
    if (!f) return false;
    bool ok = f.write(s_buf, len) == len;
    f.close();
    if (!ok) return false;
    LittleFS.remove(path);
    return LittleFS.rename(tmp, path);
}

void nodePersistBegin(NodeNames &names, PeerKeys &keys) {
    s_buf = (uint8_t *)ps_malloc(kBufCap);
    if (!s_buf) {
        Serial.println("[cs] nodes: no PSRAM, not saved");
        return;
    }
    const uint32_t now = millis();
    size_t len = readFile(kNamesPath);
    bool namesOk = len && names.fromBlob(s_buf, len, now);
    len = readFile(kKeysPath);
    bool keysOk = len && keys.fromBlob(s_buf, len, now);
    s_namesRev = names.revision();
    s_keysRev = keys.revision();
    Serial.printf("[cs] nodes: %d names%s, %d keys%s loaded\n", names.count(), namesOk ? "" : " (none saved)",
                  keys.count(), keysOk ? "" : " (none saved)");
}

static void saveNames(const NodeNames &names, uint32_t nowMs) {
    size_t len = names.toBlob(s_buf, kBufCap, nowMs);
    if (len && writeFile(kNamesPath, len)) s_namesRev = names.revision();
    s_namesAtMs = nowMs;
}

static void saveKeys(const PeerKeys &keys, uint32_t nowMs) {
    size_t len = keys.toBlob(s_buf, kBufCap, nowMs);
    if (len && writeFile(kKeysPath, len)) s_keysRev = keys.revision();
    s_keysAtMs = nowMs;
}

void nodePersistMaybeSave(const NodeNames &names, const PeerKeys &keys, uint32_t nowMs) {
    if (!s_buf) return;
    if (names.revision() != s_namesRev && nowMs - s_namesAtMs >= kSaveEveryMs) saveNames(names, nowMs);
    if (keys.revision() != s_keysRev && nowMs - s_keysAtMs >= kSaveEveryMs) saveKeys(keys, nowMs);
}

void nodePersistSaveNow(const NodeNames &names, const PeerKeys &keys) {
    if (!s_buf) return;
    const uint32_t now = millis();
    if (names.revision() != s_namesRev) saveNames(names, now);
    if (keys.revision() != s_keysRev) saveKeys(keys, now);
}
