// camillia chat server — setup and main loop wiring.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <time.h>
#include "activity_log.h"
#include "airtime.h"
#include "battery.h"
#include "battery_level.h"
#include "board.h"
#include "channel_store.h"
#include "cs_server.h"
#include "messages_json.h"
#include "mqtt_ingest.h"
#include "mesh_channel_plan.h"
#include "mesh_proto.h"
#include "node_identity.h"
#include "node_names.h"
#include "persistence.h"
#include "radio_link.h"
#include "settings.h"
#include "settings_yaml.h"
#include "status_display.h"
#include "web_ui.h"

// mesh_proto externs normally owned by camillia-mt's UI layer. PKI is unused.
uint8_t myPubKey[32]  = {0};
uint8_t myPrivKey[32] = {0};
uint8_t myDeviceRole  = 0;

static Settings     s_settings;
static uint32_t     s_nodeId = 0;
static uint32_t     s_restartAtMs = 0;
static ChannelStore s_stores[CS_MAX_CHANNELS];
static StoredMsg   *s_listBuf = nullptr;   // PSRAM scratch for the Messages tab
static CsServer      s_server;
static AirtimeBudget s_airtime;
static uint8_t       s_dutyLimitPct = 100;    // 10 in EU regions
static NodeNames     s_names;
static ActivityLog   s_activity;

// Last packet heard on LoRa, for the display's signal line.
static uint32_t s_lastRxMs = 0;
static float    s_lastRssi = 0, s_lastSnr = 0;
static bool     s_haveLastRx = false;

// Meshtastic header (16) + Data framing around our payload (portnum, length, bitfield).
static constexpr uint32_t kFrameOverhead = 16 + 8;

static uint32_t nodeIdFromMac() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    return (uint32_t)mac[2] << 24 | (uint32_t)mac[3] << 16 | (uint32_t)mac[4] << 8 | mac[5];
}

static uint32_t randomU32() { return esp_random(); }

static bool timeValid() { return time(nullptr) > 1700000000; }

static String ipText() {
    if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
    return "AP " + WiFi.softAPIP().toString();
}

static void wifiBegin() {
    WiFi.mode(s_settings.staSsid[0] ? WIFI_AP_STA : WIFI_AP);
    String ssid = String("camillia-cs-") + s_settings.shortName;
    WiFi.softAP(ssid.c_str());   // open: no password on the config AP
    if (s_settings.staSsid[0]) WiFi.begin(s_settings.staSsid, s_settings.staPass);
    Serial.printf("[cs] AP %s at %s\n", ssid.c_str(), WiFi.softAPIP().toString().c_str());
}

static const char *presetName(uint8_t i) { return i < PRESET_COUNT ? kPresets[i].channelName : nullptr; }
static int presetIndex(const char *name) {
    for (uint8_t i = 0; i < PRESET_COUNT; i++)
        if (!strcmp(kPresets[i].channelName, name) || !strcmp(kPresets[i].name, name))
            return presetUsableOnThisRadio(i) ? i : -1;
    return -1;
}
static const YamlPresetMap kYamlPresets{presetName, presetIndex};

static String lastSentJson() {
    uint32_t unix, up;
    if (!s_server.lastSentAt(unix, up)) return "null";
    return unix ? String(unix) : "\"" + String(millis() / 1000 - up) + "s ago\"";
}

static String statusJson() {
    uint32_t mqttRx, mqttDec;
    mqttCounters(mqttRx, mqttDec);
    String j = "{\"node\":\"" + String(s_settings.longName) + "\",\"nodeId\":\"!" +
               String(s_nodeId, HEX) + "\",\"ip\":\"" + ipText() + "\",\"uptimeSec\":" +
               String(millis() / 1000) + ",\"clockSet\":" + (timeValid() ? "true" : "false") + ",\"batteryV\":" + String(batteryVolts(), 2) + ",\"mqtt\":\"" + mqttState() + "\",\"mqttEnvelopes\":" + String(mqttRx) +
               ",\"mqttDecrypted\":" + String(mqttDec) + ",\"mqttDropped\":" + mqttDiagJson() + ",\"lastSent\":" + lastSentJson() +
               ",\"channels\":[";
    for (int i = 0; i < s_settings.chanCount; i++) {
        if (i) j += ",";
        j += "{\"name\":\"" + String(s_settings.ch[i].name) + "\",\"messages\":" +
             String(s_stores[i].count()) + ",\"headSeq\":" + String(s_stores[i].headSeq()) + "}";
    }
    j += "]}";
    return j;
}

static void listMessages(int slot, const std::function<void(const char *)> &emit) {
    if (!s_listBuf) s_listBuf = (StoredMsg *)ps_malloc(sizeof(StoredMsg) * CS_MSGS_PER_CHANNEL);
    if (!s_listBuf) return;
    const ChannelStore &st = s_stores[slot];
    int n = st.copyAfter(st.tailSeq() - 1, s_listBuf, CS_MSGS_PER_CHANNEL);
    uint32_t nowUnix = (uint32_t)time(nullptr), up = millis() / 1000;
    char json[600];
    for (int i = n - 1; i >= 0; i--) {
        uint32_t age = messageAgeSec(s_listBuf[i], nowUnix, timeValid(), up);
        if (messageJson(s_listBuf[i], age, json, sizeof json)) emit(json);
    }
}

static String storageJson() {
    String j = "{\"channels\":[";
    for (int i = 0; i < s_settings.chanCount; i++) {
        const ChannelStore &st = s_stores[i];
        StoredMsg first{}, last{};
        if (st.count()) {
            st.copyAfter(st.tailSeq() - 1, &first, 1);
            st.copyAfter(st.headSeq() - 1, &last, 1);
        }
        if (i) j += ",";
        j += "{\"name\":\"" + String(s_settings.ch[i].name) + "\",\"count\":" + String(st.count()) +
             ",\"capacity\":" + String(CS_MSGS_PER_CHANNEL) + ",\"oldestUnix\":" + String(first.rxUnix) +
             ",\"newestUnix\":" + String(last.rxUnix) + ",\"textBytes\":" + String(st.textBytes()) +
             ",\"ramBytes\":" + String(ChannelStore::ramBytes()) + ",\"fileBytes\":" + String(persistFileSize(i)) + "}";
    }
    size_t used, total;
    persistUsage(used, total);
    j += "],\"flash\":{\"usedKB\":" + String(used / 1024.0, 1) + ",\"totalKB\":" + String(total / 1024.0, 1) +
         "},\"psram\":{\"freeKB\":" + String(ESP.getFreePsram() / 1024.0, 1) +
         ",\"totalKB\":" + String(ESP.getPsramSize() / 1024.0, 1) + "}}";
    return j;
}

static void clearMessages(int slot) {
    for (int i = 0; i < s_settings.chanCount; i++) {
        if (slot != -1 && slot != i) continue;
        s_server.clearSlot(i);
        s_stores[i].reset(esp_random());   // new epoch: nodes' cursors fall back cleanly
        persistSaveNow(s_stores[i], i);
        Serial.printf("[cs] cleared channel %d \"%s\", new epoch %08x\n", i, s_settings.ch[i].name,
                      (unsigned)s_stores[i].epoch());
    }
}

static const PresetParams &preset() {
    return kPresets[s_settings.modemPreset < PRESET_COUNT ? s_settings.modemPreset : PRESET_LONG_FAST];
}

static uint32_t airMsFor(size_t payloadLen) {
    const PresetParams &p = preset();
    return timeOnAirMs((uint32_t)(payloadLen + kFrameOverhead), p.sf, p.bw, p.cr, 16);
}

static void serverBegin() {
    ServerConfig cfg;
    cfg.maxHops = s_settings.maxHops;
    cfg.batchSize = s_settings.batchSize;
    cfg.packetGapMs = s_settings.packetGapMs;
    cfg.myNodeId = s_nodeId;
    cfg.beforeServe = [](int slot) {
        if (s_stores[slot].dirty()) persistSaveNow(s_stores[slot], slot);
    };
    strncpy(cfg.shortName, s_settings.shortName, sizeof(cfg.shortName) - 1);
    static uint32_t ids[CS_MAX_CHANNELS];
    static char names[CS_MAX_CHANNELS][12];
    for (int i = 0; i < s_settings.chanCount; i++) {
        ids[i] = csp::channelId(s_settings.ch[i].name, s_settings.ch[i].key, s_settings.ch[i].keyLen);
        strncpy(names[i], s_settings.ch[i].name, sizeof(names[i]) - 1);
    }
    s_server.begin(cfg, s_stores, ids, names, s_settings.chanCount);
    s_dutyLimitPct = regionDutyPct(s_settings.region);
    Serial.printf("[cs] server: batch %u, gap %u ms, max hops %u, duty limit %u%%\n",
                  cfg.batchSize, cfg.packetGapMs, cfg.maxHops, s_dutyLimitPct);
}

static void logActivity(ActivityKind kind, uint32_t node, int slot, uint32_t a = 0, uint32_t b = 0,
                        uint8_t flags = 0) {
    ActivityEntry e{};
    e.uptimeSec = millis() / 1000;
    e.unix = timeValid() ? (uint32_t)time(nullptr) : 0;
    e.kind = kind;
    e.node = node;
    e.chanSlot = (int8_t)slot;
    e.a = a;
    e.b = b;
    e.flags = flags;
    s_activity.add(e);
}

// What logOutgoing decoded, so serveLoop doesn't decode the packet again.
struct TxInfo {
    bool    isBatch = false;
    uint8_t flags = 0;   // csp::FLAG_* of the BATCH header
    uint8_t items = 0;
};

static TxInfo logOutgoing(const Outgoing &o, bool ok) {
    TxInfo info;
    csp::Type t;
    if (csp::peekType(o.payload, o.len, t) && t == csp::BATCH) {
        csp::BatchHeader h;
        static csp::Item items[16];
        uint8_t n = 0;
        if (csp::decodeBatch(o.payload, o.len, h, items, 16, n)) {
            Serial.printf("[cs] tx BATCH to !%08x ch%d hops %u: %u item(s)%s%s%s, seq %lu..%lu, %u bytes %s\n",
                          (unsigned)o.to, o.chanSlot, o.hopLimit, n,
                          (h.flags & csp::FLAG_FIRST) ? " FIRST" : "",
                          (h.flags & csp::FLAG_LAST) ? " LAST" : "", (h.flags & csp::FLAG_MORE) ? " MORE" : "",
                          n ? (unsigned long)items[0].seq : 0UL, n ? (unsigned long)items[n - 1].seq : 0UL,
                          (unsigned)o.len, ok ? "ok" : "FAILED");
            info.isBatch = true;
            info.flags = h.flags;
            info.items = n;
            return info;
        }
    }
    Serial.printf("[cs] tx ANNOUNCE to !%08x hops %u, %u bytes %s\n", (unsigned)o.to, o.hopLimit,
                  (unsigned)o.len, ok ? "ok" : "FAILED");
    return info;
}

static void serveLoop(uint32_t nowMs) {
    // Check the budget against a worst-case packet before poll() dequeues one.
    if (!s_airtime.canSend(nowMs, airMsFor(csp::MAX_PAYLOAD), s_dutyLimitPct)) {
        // Only worth a log line when there is something waiting to go out.
        static uint32_t lastHeldMs = 0;
        static bool heldLogged = false;
        if ((s_server.queueLength() > 0 || s_server.busy()) &&
            (!heldLogged || nowMs - lastHeldMs >= 60000)) {
            heldLogged = true;
            lastHeldMs = nowMs;
            logActivity(ACT_HELD, 0, -1);
        }
        return;
    }
    static Outgoing out;
    if (!s_server.poll(nowMs, (uint32_t)time(nullptr), timeValid(), nowMs / 1000, out)) return;
    bool ok = radioSend(out.to, out.chanSlot, out.hopLimit, PORT_CHAT_SERVER, out.payload, out.len);
    s_airtime.record(millis(), airMsFor(out.len));
    TxInfo tx = logOutgoing(out, ok);

    if (!ok) logActivity(ACT_TX_FAIL, out.to, out.chanSlot);
    if (!tx.isBatch) {
        if (ok) logActivity(ACT_ANNOUNCE, out.to, -1);
        return;
    }
    // Sum a transfer's packets per (to, slot); one ACT_BATCH when the last goes out.
    static struct { uint32_t to; int slot; uint32_t packets, msgs; bool active; } run;
    if ((tx.flags & csp::FLAG_FIRST) || !run.active || run.to != out.to || run.slot != out.chanSlot)
        run = {out.to, out.chanSlot, 0, 0, true};
    if (ok) {
        run.packets++;
        run.msgs += tx.items;
    }
    if (tx.flags & csp::FLAG_LAST) {
        logActivity(ACT_BATCH, run.to, run.slot, run.packets, run.msgs, (tx.flags & csp::FLAG_MORE) ? 1 : 0);
        run.active = false;
    }
}

static void handleChatServerPacket(const MeshPacket &pkt, uint32_t nowMs) {
    int slot = slotForPacket(pkt);
    if (slot == -2 || pkt.hdr.from == s_nodeId) return;
    csp::Type t;
    if (!csp::peekType(pkt.payload, pkt.payloadLen, t)) return;
    if (t == csp::DISCOVER) {
        logActivity(ACT_DISCOVER, pkt.hdr.from, slot);
    } else if (t == csp::REQUEST) {
        csp::Request r{};
        uint32_t cursor = 0;
        if (slot >= 0 && csp::decodeRequest(pkt.payload, pkt.payloadLen, r)) cursor = r.cursor;
        logActivity(ACT_REQUEST, pkt.hdr.from, slot, cursor);
    }
    Serial.printf("[cs] rx %s from !%08x ch%d hops %u\n", t == csp::DISCOVER ? "DISCOVER" : t == csp::REQUEST ? "REQUEST" : "other",
                  (unsigned)pkt.hdr.from, slot, hopsTravelled(pkt.hdr));
    s_server.onPacket(pkt.hdr.from, pkt.hdr.to, pkt.hdr.id, slot, hopsTravelled(pkt.hdr), pkt.payload,
                      pkt.payloadLen, nowMs);
}

// USB serial console: "yaml-export" prints the config; "yaml-import" reads YAML
// until a line "---end---", then validates, saves and restarts. The test build
// (heltec-v4-test) also accepts "req <slot> <cursor>" and "disc", which fake a
// node 0x12345678 at 0 hops.
static void serialLoop(uint32_t nowMs) {
    static String line, yaml;
    static bool importing = false;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c != '\n' && c != '\r') { line += c; continue; }
        if (importing) {
            if (line == "---end---") {
                importing = false;
                Settings n = s_settings;
                char err[96] = "";
                if (settingsFromYaml(yaml.c_str(), n, kYamlPresets, err, sizeof err) &&
                    settingsValidateDevice(n, err, sizeof err)) {
                    s_settings = n;
                    settingsSave(n);
                    Serial.println("[cs] yaml-import: saved, restarting");
                    s_restartAtMs = millis() + 500;
                } else {
                    Serial.printf("[cs] yaml-import failed: %s\n", err);
                }
                yaml = "";
            } else {
                yaml += line + "\n";
            }
            line = "";
            continue;
        }
        if (!line.length()) continue;
        if (line == "yaml-export") {
            Serial.print(settingsToYaml(s_settings, kYamlPresets).c_str());
            Serial.println("---end---");
        } else if (line == "yaml-import") {
            importing = true;
            yaml = "";
            Serial.println("[cs] yaml-import: paste YAML, finish with a line ---end---");
#ifdef CS_SERIAL_TEST
        } else {
            uint8_t buf[csp::MAX_PAYLOAD];
            size_t n = 0;
            int slot = -1;
            unsigned long cursor = 0;
            if (sscanf(line.c_str(), "req %d %lu", &slot, &cursor) >= 1 && slot >= 0 && slot < s_settings.chanCount) {
                csp::Request r{cursor ? s_stores[slot].epoch() : 0, (uint32_t)cursor, 0, 0};
                n = csp::encodeRequest(r, buf, sizeof buf);
            } else if (line == "disc") {
                slot = -1;
                n = csp::encodeDiscover(buf, sizeof buf);
            }
            if (n) {
                Serial.printf("[cs] test: %s\n", line.c_str());
                static uint32_t testPktId = 1;
                s_server.onPacket(0x12345678, slot < 0 ? 0xFFFFFFFF : s_nodeId, testPktId++, slot, 0, buf, n, nowMs);
            } else {
                Serial.printf("[cs] unknown command \"%s\"\n", line.c_str());
            }
#endif
        }
        line = "";
    }
}

static void ingest(const MeshPacket &pkt, MsgSource source) {
    if (pkt.portnum == NODEINFO_APP && pkt.hdr.from != s_nodeId) {
        UserInfo u;
        if (decodeUser(pkt.payload, pkt.payloadLen, u)) s_names.update(pkt.hdr.from, u.longName, u.shortName, millis());
    }
    int slot = slotForPacket(pkt);
    if (pkt.portnum != TEXT_MESSAGE_APP || pkt.hdr.to != 0xFFFFFFFF || slot < 0) return;
    time_t now = time(nullptr);
    bool added = s_stores[slot].add(pkt.hdr.from, pkt.hdr.id, (const char *)pkt.payload,
                                    pkt.payloadLen, timeValid() ? (uint32_t)now : 0,
                                    millis() / 1000, source);
    Serial.printf("[cs] text ch%d from !%08x id %08x via %s: %s\n", slot, (unsigned)pkt.hdr.from,
                  (unsigned)pkt.hdr.id, source == SRC_MQTT ? "mqtt" : "lora", added ? "stored" : "duplicate");
}

void setup() {
#if defined(CS_BOARD_V4_EXPANSION)
    // VEXT (GPIO36) LOW first, and never again: HIGH breaks touch I2C, and
    // raising it late stops this board booting. See board_v4_exp.h.
    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, VEXT_ON_LEVEL);
    // GPIO7 (BOARD_POWERON in camillia-mt main_lvgl.cpp setup()) HIGH before lcd.init(),
    // not only later in radioBegin().
    pinMode(LORA_FEM_POWER_PIN, OUTPUT);
    digitalWrite(LORA_FEM_POWER_PIN, HIGH);
#endif
    Serial.begin(115200);
    delay(1500);
    s_nodeId = nodeIdFromMac();
    if (!settingsLoad(s_settings)) {
        settingsDefaults(s_settings, s_nodeId);
        settingsSave(s_settings);
        Serial.println("[cs] settings: defaults written");
    }
    Serial.printf("[cs] boot node !%08x \"%s\" (%s)\n", (unsigned)s_nodeId, s_settings.longName,
                  s_settings.shortName);

    s_names.begin(ps_malloc);   // no PSRAM: names just stay hex
    displayBegin();
    batteryBegin();
    wifiBegin();
    configTzTime(s_settings.tz, "pool.ntp.org");
    webBegin(&s_settings, [] { s_restartAtMs = millis() + 1500; }, statusJson, listMessages, clearMessages, storageJson);

    static_assert(CS_MAX_CHANNELS == SETTINGS_MAX_CHANNELS, "store and settings channel limits differ");
    // Only configured channels get a ring (~55 KB of PSRAM each).
    for (int i = 0; i < s_settings.chanCount; i++) {
        if (!s_stores[i].begin(ps_malloc, randomU32)) Serial.printf("[cs] store %d: no PSRAM\n", i);
    }
    persistBegin();
    persistLoadAll(s_stores, s_settings);

    radioBegin(s_settings, s_nodeId);
    mqttBegin(s_settings, s_nodeId, [](const MeshPacket &p) { ingest(p, SRC_MQTT); });
    serverBegin();
    identityBegin(s_settings, s_nodeId);
}

void loop() {
    const uint32_t nowMs = millis();
    webLoop();

    static bool staLogged = false;
    if (!staLogged && WiFi.status() == WL_CONNECTED) {
        staLogged = true;
        Serial.printf("[cs] wifi joined \"%s\" as %s\n", s_settings.staSsid, WiFi.localIP().toString().c_str());
    }

    MeshPacket pkt;
    while (radioPoll(pkt)) {
        s_lastRxMs = millis();
        s_lastRssi = pkt.rssi;
        s_lastSnr = pkt.snr;
        s_haveLastRx = true;
        if (pkt.portnum == PORT_CHAT_SERVER) handleChatServerPacket(pkt, nowMs);
        else ingest(pkt, SRC_LORA);
    }
    serialLoop(nowMs);
    serveLoop(nowMs);
    batteryLoop(nowMs);
    mqttLoop(nowMs);

    persistMaybeSave(s_stores, s_settings.chanCount, nowMs);
    identityLoop(nowMs);

    static DisplayStatus st;   // static: keeps the loop's stack small
    st = DisplayStatus{};
    st.nodeName = s_settings.longName;
    snprintf(st.ip, sizeof st.ip, "%s", ipText().c_str());
    st.mqtt = mqttState();
    for (int i = 0; i < s_settings.chanCount; i++) st.storedMessages += s_stores[i].count();
    st.battState = batteryStateFromVolts(batteryVolts());
    st.battPct = batteryPercentFromVolts(batteryVolts());
    st.settings = &s_settings;
    st.nodeId = s_nodeId;
    st.uptimeSec = nowMs / 1000;
    st.clockSet = timeValid();
    st.battVolts = batteryVolts();
    mqttCounters(st.mqttRx, st.mqttDecrypted);
    st.haveLastRx = s_haveLastRx;
    st.lastRxAgeSec = (millis() - s_lastRxMs) / 1000;
    st.lastRssi = s_lastRssi;
    st.lastSnr = s_lastSnr;
    st.airtimePct = 100.0f * s_airtime.usedMs(nowMs) / AirtimeBudget::WINDOW_MS;
    st.dutyLimitPct = s_dutyLimitPct;
    st.chanCount = s_settings.chanCount;
    for (int i = 0; i < s_settings.chanCount; i++) st.chanCounts[i] = s_stores[i].count();
    {
        // Flash usage walks the filesystem: refresh every 10 s at most.
        static uint32_t flashAtMs = 0, flashUsedKB = 0, flashTotalKB = 0;
        static bool flashRead = false;
        if (!flashRead || nowMs - flashAtMs >= 10000) {
            size_t used = 0, total = 0;
            persistUsage(used, total);
            flashUsedKB = (uint32_t)(used / 1024);
            flashTotalKB = (uint32_t)(total / 1024);
            flashAtMs = nowMs;
            flashRead = true;
        }
        st.flashUsedKB = flashUsedKB;
        st.flashTotalKB = flashTotalKB;
    }
    st.psramFreeKB = (uint32_t)(ESP.getFreePsram() / 1024);
    st.newestMessages = [](StoredMsg *out, int8_t *slots, int max) {
        return newestAcross(s_stores, s_settings.chanCount, out, slots, max);
    };
    st.newestActivity = [](ActivityEntry *out, int max) { return s_activity.copyNewest(out, max); };
    st.chanName = [](int slot) -> const char * {
        return slot >= 0 && slot < s_settings.chanCount ? s_settings.ch[slot].name : nullptr;
    };
    st.names = &s_names;
    displayUpdate(st);

    if (s_restartAtMs && (int32_t)(nowMs - s_restartAtMs) >= 0) {
        // Don't lose messages heard since the last scheduled save.
        for (int i = 0; i < s_settings.chanCount; i++)
            if (s_stores[i].dirty()) persistSaveNow(s_stores[i], i);
        ESP.restart();
    }
}
