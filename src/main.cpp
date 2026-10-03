// camillia chat server — setup and main loop wiring.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <time.h>
#include "airtime.h"
#include "channel_store.h"
#include "cs_server.h"
#include "messages_json.h"
#include "mesh_channel_plan.h"
#include "mesh_proto.h"
#include "node_identity.h"
#include "persistence.h"
#include "radio_link.h"
#include "settings.h"
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

static String statusJson() {
    String j = "{\"node\":\"" + String(s_settings.longName) + "\",\"nodeId\":\"!" +
               String(s_nodeId, HEX) + "\",\"ip\":\"" + ipText() + "\",\"uptimeSec\":" +
               String(millis() / 1000) + ",\"clockSet\":" + (timeValid() ? "true" : "false") +
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
    strncpy(cfg.shortName, s_settings.shortName, sizeof(cfg.shortName) - 1);
    static uint32_t ids[CS_MAX_CHANNELS];
    static char names[CS_MAX_CHANNELS][12];
    for (int i = 0; i < s_settings.chanCount; i++) {
        ids[i] = csp::channelId(s_settings.ch[i].name, s_settings.ch[i].key, s_settings.ch[i].keyLen);
        strncpy(names[i], s_settings.ch[i].name, sizeof(names[i]) - 1);
    }
    s_server.begin(cfg, s_stores, ids, names, s_settings.chanCount);
    s_dutyLimitPct = (!strcmp(s_settings.region, "EU_868") || !strcmp(s_settings.region, "EU_433")) ? 10 : 100;
    Serial.printf("[cs] server: batch %u, gap %u ms, max hops %u, duty limit %u%%\n",
                  cfg.batchSize, cfg.packetGapMs, cfg.maxHops, s_dutyLimitPct);
}

static void logOutgoing(const Outgoing &o, bool ok) {
    csp::Type t;
    if (csp::peekType(o.payload, o.len, t) && t == csp::BATCH) {
        csp::BatchHeader h;
        static csp::Item items[16];
        uint8_t n = 0;
        if (csp::decodeBatch(o.payload, o.len, h, items, 16, n)) {
            Serial.printf("[cs] tx BATCH to !%08x ch%d hops %u: %u item(s)%s%s, seq %lu..%lu, %u bytes %s\n",
                          (unsigned)o.to, o.chanSlot, o.hopLimit, n,
                          (h.flags & csp::FLAG_LAST) ? " LAST" : "", (h.flags & csp::FLAG_MORE) ? " MORE" : "",
                          n ? (unsigned long)items[0].seq : 0UL, n ? (unsigned long)items[n - 1].seq : 0UL,
                          (unsigned)o.len, ok ? "ok" : "FAILED");
            return;
        }
    }
    Serial.printf("[cs] tx ANNOUNCE to !%08x hops %u, %u bytes %s\n", (unsigned)o.to, o.hopLimit,
                  (unsigned)o.len, ok ? "ok" : "FAILED");
}

static void serveLoop(uint32_t nowMs) {
    // Check the budget against a worst-case packet before poll() dequeues one.
    if (!s_airtime.canSend(nowMs, airMsFor(csp::MAX_PAYLOAD), s_dutyLimitPct)) return;
    static Outgoing out;
    if (!s_server.poll(nowMs, (uint32_t)time(nullptr), timeValid(), nowMs / 1000, out)) return;
    bool ok = radioSend(out.to, out.chanSlot, out.hopLimit, PORT_CHAT_SERVER, out.payload, out.len);
    s_airtime.record(millis(), airMsFor(out.len));
    logOutgoing(out, ok);
}

static void handleChatServerPacket(const MeshPacket &pkt, uint32_t nowMs) {
    int slot = slotForChanIdx(pkt.chanIdx);
    if (slot == -2 || pkt.hdr.from == s_nodeId) return;
    csp::Type t;
    if (!csp::peekType(pkt.payload, pkt.payloadLen, t)) return;
    Serial.printf("[cs] rx %s from !%08x ch%d hops %u\n", t == csp::DISCOVER ? "DISCOVER" : t == csp::REQUEST ? "REQUEST" : "other",
                  (unsigned)pkt.hdr.from, slot, hopsTravelled(pkt.hdr));
    s_server.onPacket(pkt.hdr.from, slot, hopsTravelled(pkt.hdr), pkt.payload, pkt.payloadLen, nowMs);
}

#ifdef CS_SERIAL_TEST
// Bench hook: "req <slot> <cursor>" / "disc" fake a node 0x12345678 at 0 hops.
static void serialTestLoop(uint32_t nowMs) {
    static String line;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c != '\n' && c != '\r') { line += c; continue; }
        if (!line.length()) continue;
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
            s_server.onPacket(0x12345678, slot, 0, buf, n, nowMs);
        } else {
            Serial.printf("[cs] test: unknown \"%s\" (use: req <slot> <cursor> | disc)\n", line.c_str());
        }
        line = "";
    }
}
#endif

static void ingest(const MeshPacket &pkt) {
    int slot = slotForChanIdx(pkt.chanIdx);
    if (pkt.portnum != TEXT_MESSAGE_APP || pkt.hdr.to != 0xFFFFFFFF || slot < 0) return;
    time_t now = time(nullptr);
    bool added = s_stores[slot].add(pkt.hdr.from, pkt.hdr.id, (const char *)pkt.payload,
                                    pkt.payloadLen, timeValid() ? (uint32_t)now : 0,
                                    millis() / 1000, SRC_LORA);
    Serial.printf("[cs] text ch%d from !%08x id %08x: %s\n", slot, (unsigned)pkt.hdr.from,
                  (unsigned)pkt.hdr.id, added ? "stored" : "duplicate");
}

void setup() {
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

    displayBegin();
    wifiBegin();
    configTzTime(s_settings.tz, "pool.ntp.org");
    webBegin(&s_settings, [] { s_restartAtMs = millis() + 1500; }, statusJson, listMessages);

    for (int i = 0; i < CS_MAX_CHANNELS; i++) {
        if (!s_stores[i].begin(ps_malloc, randomU32)) Serial.printf("[cs] store %d: no PSRAM\n", i);
    }
    persistBegin();
    persistLoadAll(s_stores, s_settings);

    radioBegin(s_settings, s_nodeId);
    serverBegin();
    identityBegin(s_settings, s_nodeId);
}

void loop() {
    const uint32_t nowMs = millis();
    webLoop();

    MeshPacket pkt;
    while (radioPoll(pkt)) {
        if (pkt.portnum == PORT_CHAT_SERVER) handleChatServerPacket(pkt, nowMs);
        else ingest(pkt);
    }
#ifdef CS_SERIAL_TEST
    serialTestLoop(nowMs);
#endif
    serveLoop(nowMs);

    persistMaybeSave(s_stores, s_settings.chanCount, nowMs);
    identityLoop(nowMs);

    DisplayStatus st{};
    st.nodeName = s_settings.longName;
    snprintf(st.ip, sizeof st.ip, "%s", ipText().c_str());
    st.mqtt = "off";
    st.everSent = s_server.lastSentAt(st.lastSentUnix, st.lastSentUptimeSec);
    displayUpdate(st);

    if (s_restartAtMs && (int32_t)(nowMs - s_restartAtMs) >= 0) ESP.restart();
}
