// camillia chat server — setup and main loop wiring.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <time.h>
#include "activity_log.h"
#include "airtime.h"
#include "battery.h"
#include "battery_level.h"
#include "board_init.h"
#include "cfg_cli.h"
#include "channel_store.h"
#include "community_preset_lines.h"
#include "community_presets.h"
#include "cs_server.h"
#include "logo_store.h"
#include "messages_json.h"
#include "mqtt_ingest.h"
#include "mesh_channel_plan.h"
#include "mesh_proto.h"
#include "node_identity.h"
#include "node_names.h"
#include "peer_keys.h"
#include "pki_identity.h"
#include "ota_update.h"
#include "persistence.h"
#include "radio_link.h"
#include "settings.h"
#include "settings_yaml.h"
#include "status_display.h"
#include "web_ui.h"

// mesh_proto externs normally owned by camillia-mt's UI layer. The key pair is
// pkiBegin()'s.
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
static PeerKeys      s_peerKeys;
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

static String apSsid() { return String("camillia-cs-") + s_settings.shortName; }

static void apStart() {
    WiFi.softAP(apSsid().c_str());   // open: no password on the config AP
    Serial.printf("[cs] AP %s at %s\n", apSsid().c_str(), WiFi.softAPIP().toString().c_str());
}

static void wifiBegin() {
    WiFi.mode(s_settings.staSsid[0] ? WIFI_AP_STA : WIFI_AP);
    WiFi.setAutoReconnect(true);
    apStart();
    if (s_settings.staSsid[0]) WiFi.begin(s_settings.staSsid, s_settings.staPass);
}

// The config AP is only up while the home network isn't: it goes down once the
// station joins, and comes back if the station has been off it for 30 s.
static void wifiLoop(uint32_t nowMs) {
    static constexpr uint32_t kApBackAfterMs = 30000;
    static uint32_t staDownSinceMs = 0;
    if (!s_settings.staSsid[0]) return;
    const bool joined = WiFi.status() == WL_CONNECTED;
    const bool apOn = WiFi.getMode() & WIFI_MODE_AP;
    if (joined) {
        staDownSinceMs = 0;
        if (apOn) {
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            Serial.printf("[cs] wifi joined \"%s\" as %s; config AP off\n", s_settings.staSsid,
                          WiFi.localIP().toString().c_str());
        }
        return;
    }
    if (!staDownSinceMs) staDownSinceMs = nowMs ? nowMs : 1;
    if (!apOn && nowMs - staDownSinceMs >= kApBackAfterMs) {
        Serial.printf("[cs] wifi \"%s\" lost; config AP back on\n", s_settings.staSsid);
        WiFi.mode(WIFI_AP_STA);
        apStart();
    }
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
    uint32_t unixTime, up;
    if (!s_server.lastSentAt(unixTime, up)) return "null";
    return unixTime ? String(unixTime) : "\"" + String(millis() / 1000 - up) + "s ago\"";
}

static const char *const kBattNames[] = {"absent", "external", "present"};

static String statusJson() {
    uint32_t mqttRx, mqttDec;
    mqttCounters(mqttRx, mqttDec);
    String j = "{\"node\":\"" + String(s_settings.longName) + "\",\"nodeId\":\"!" +
               String(s_nodeId, HEX) + "\",\"ip\":\"" + ipText() + "\",\"uptimeSec\":" +
               String(millis() / 1000) + ",\"clockSet\":" + (timeValid() ? "true" : "false") + ",\"batteryV\":" + String(batteryVolts(), 2) + ",\"batteryPct\":" + String(batteryPercentFromVolts(batteryVolts())) + ",\"battery\":\"" + kBattNames[batteryStateFromVolts(batteryVolts())] + "\"" + ",\"mqtt\":\"" + mqttState() + "\",\"mqttEnvelopes\":" + String(mqttRx) +
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
    const int cap = s_settings.msgsPerChannel;
    if (!s_listBuf) s_listBuf = (StoredMsg *)ps_malloc(ChannelStore::ramBytes(cap));
    if (!s_listBuf) return;
    const ChannelStore &st = s_stores[slot];
    int n = st.copyAfter(st.tailSeq() - 1, s_listBuf, cap);
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
             ",\"capacity\":" + String(st.capacity()) + ",\"oldestUnix\":" + String(first.rxUnix) +
             ",\"newestUnix\":" + String(last.rxUnix) + ",\"textBytes\":" + String(st.textBytes()) +
             ",\"ramBytes\":" + String(st.ramBytes()) + ",\"fileBytes\":" + String(persistFileSize(i)) + "}";
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
    e.unixTime = timeValid() ? (uint32_t)time(nullptr) : 0;
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

// ── cfg CLI (cfg_cli.h): the web flasher's Customize, and community presets ──
static int cfgPresetFromName(const char *name) {
    for (uint8_t i = 0; i < PRESET_COUNT; i++)
        if (!strcasecmp(kPresets[i].channelName, name)) return presetUsableOnThisRadio(i) ? i : -1;
    return -1;
}
static bool cfgValidate(const Settings &s, char *err, size_t cap) {
    return settingsValidateDevice(s, err, cap) && persistFits(s, err, cap);
}
static char     s_cfgPresetList[200];
static CfgHooks s_cfgHooks;
static CfgCli  *s_cfg = nullptr;

static void cfgBegin() {
    size_t n = 0;
    for (uint8_t i = 0; i < PRESET_COUNT; i++)
        if (presetUsableOnThisRadio(i))
            n += snprintf(s_cfgPresetList + n, sizeof s_cfgPresetList - n, "%s%s", n ? "," : "",
                          kPresets[i].channelName);
    s_cfgHooks.presetFromName = cfgPresetFromName;
    s_cfgHooks.presetName = presetName;
    s_cfgHooks.regionKnown = regionKnown;
    s_cfgHooks.validate = cfgValidate;
    s_cfgHooks.presetList = s_cfgPresetList;
    s_cfgHooks.hasLogo = displayLogoBox(s_cfgHooks.logoW, s_cfgHooks.logoH);
    s_cfg = new CfgCli(s_cfgHooks);
}

// Runs one cfg command (without the leading "cfg "), replying in `reply`. A
// commit clears every channel's history (a customization usually means another
// mesh), saves the settings and the logo, and restarts. False if the reply is
// an error.
static bool applyCfgLine(const char *line, char *reply, size_t cap) {
    if (s_cfg->handle(line, s_settings, reply, cap) == CfgResult::Commit) {
        clearMessages(-1);
        if (s_cfg->logoStaged()) logoSave(s_cfg->logoData(), s_cfg->logoLen());
        else if (s_cfg->logoClear()) logoErase();
        settingsSave(s_cfg->staged());
        s_cfg->reset();
        s_restartAtMs = millis() + 500;
    }
    return strncmp(reply, "[cfg] err", 9) != 0;
}

const char *communityPresetProblem(int idx) {
    if (idx < 0 || idx >= kCommunityPresetCount) return "No such community preset";
    const CommunityPreset &p = kCommunityPresets[idx];
    if (!regionKnown(p.region)) return "Its region is not one this server knows";
    if (p.preset[0] && cfgPresetFromName(p.preset) < 0) return "Its radio preset needs a radio this board does not have";
    if (p.mqtt.host[0] && p.mqtt.tls) return "Its MQTT broker needs TLS, which the server does not have";
    return nullptr;
}

// Applies community preset idx through the cfg CLI; on success the commit clears
// history, saves and restarts. False with the reason in err otherwise, with
// nothing staged left behind.
static bool communityPresetApply(int idx, char *err, size_t cap) {
    if (const char *why = communityPresetProblem(idx)) {
        snprintf(err, cap, "%s", why);
        return false;
    }
    static CfgPresetLine lines[CFG_PRESET_MAX_LINES];
    const int n = communityPresetLines(idx, lines, CFG_PRESET_MAX_LINES);
    char reply[256];
    s_cfg->reset();
    for (int i = 0; i < n; i++) {
        if (!applyCfgLine(lines[i].text, reply, sizeof reply)) {
            s_cfg->reset();
            snprintf(err, cap, "%s", reply + (strncmp(reply, "[cfg] err ", 10) ? 0 : 10));
            return false;
        }
    }
    Serial.printf("[cs] community preset %s applied\n", kCommunityPresets[idx].id);
    return n > 0;
}

#ifdef CS_SERIAL_TEST
// The test build's "req <slot> <cursor>" and "disc", which fake a node
// 0x12345678 at 0 hops. False if `line` is neither.
static bool serialTestCommand(const String &line, uint32_t nowMs) {
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
    if (!n) return false;
    Serial.printf("[cs] test: %s\n", line.c_str());
    static uint32_t testPktId = 1;
    s_server.onPacket(0x12345678, slot < 0 ? 0xFFFFFFFF : s_nodeId, testPktId++, slot, 0, buf, n, nowMs);
    return true;
}
#endif

// USB serial console: "yaml-export" prints the config; "yaml-import" reads YAML
// until a line "---end---", then validates, saves and restarts; "cfg ..." is the
// cfg CLI. Lines over 256 bytes are dropped, as on camillia-mt.
static void serialLoop(uint32_t nowMs) {
    static constexpr size_t kLineMax = 256;
    static String line, yaml;
    static bool importing = false, overlong = false;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c != '\n' && c != '\r') {
            if (line.length() < kLineMax) line += c;
            else overlong = true;
            continue;
        }
        if (overlong) {
            overlong = false;
            line = "";
            Serial.println("[cfg] err line too long");
            continue;
        }
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
        } else if (line == "cfg" || line.startsWith("cfg ")) {
            char reply[256];
            applyCfgLine(line.c_str() + (line.length() > 3 ? 4 : 3), reply, sizeof reply);
            Serial.println(reply);
#ifdef CS_SERIAL_TEST
        } else if (serialTestCommand(line, nowMs)) {
#endif
        } else {
            const int sp = line.indexOf(' ');
            Serial.printf("[cli] unknown command: %s\n", (sp < 0 ? line : line.substring(0, sp)).c_str());
        }
        line = "";
    }
}

// Reply to the "status" DM command.
static String statusText() {
    uint32_t up = millis() / 1000;
    char buf[48];
    snprintf(buf, sizeof buf, "up %lud%luh%lum", (unsigned long)(up / 86400), (unsigned long)(up / 3600 % 24),
             (unsigned long)(up / 60 % 60));
    String t = String(s_settings.longName) + ": " + buf;
    if (batteryStateFromVolts(batteryVolts()) == BATT_PRESENT) t += ", batt " + String(batteryPercentFromVolts(batteryVolts())) + "%";
    t += String(", mqtt ") + mqttState() + ", air " +
         String(100.0f * s_airtime.usedMs(millis()) / AirtimeBudget::WINDOW_MS, 1) + "%";
    for (int i = 0; i < s_settings.chanCount; i++)
        t += String(i ? "; " : ", ") + s_settings.ch[i].name + " " + String(s_stores[i].count()) + " msgs";
    return t;
}

static String helpText();

// Commands a node can DM to the server; each returns the reply text.
static const struct { const char *name; const char *desc; String (*run)(); } kDmCommands[] = {
    {"help", "list commands", helpText},
    {"status", "server status", statusText},
};

static String helpText() {
    String t = "Commands:";
    for (const auto &c : kDmCommands) t += String("\n") + c.name + " - " + c.desc;
    return t;
}

// Routing ACK for a packet addressed here that asked for one, so the sender's
// DM shows as delivered. On the channel it came in on; a PKI packet has none,
// so the primary, as camillia-mt and Meshtastic do.
static void sendAck(const MeshPacket &pkt, int slot) {
    if (!(pkt.hdr.flags & (1 << 3)) || pkt.hdr.to != s_nodeId) return;
    uint8_t buf[64];
    size_t n = encodeRouting(pkt.hdr.id, s_nodeId, 0, buf, sizeof buf);
    if (!n || !s_airtime.canSend(millis(), airMsFor(n), s_dutyLimitPct)) return;
    if (radioSendData(pkt.hdr.from, slot, s_settings.maxHops, buf, n)) s_airtime.record(millis(), airMsFor(n));
}

static bool handleDmCommand(const MeshPacket &pkt) {
    if (pkt.portnum != TEXT_MESSAGE_APP || pkt.hdr.to != s_nodeId || pkt.hdr.from == s_nodeId) return false;
    // camillia-mt sends DMs PKI-encrypted only (chanIdx -2); stock Meshtastic
    // falls back to a channel key when it has no key for us.
    const bool pki = pkt.chanIdx == -2;
    int slot = pki ? 0 : slotForPacket(pkt);
    if (slot == -2) return false;
    sendAck(pkt, slot);
    char text[sizeof pkt.payload + 1];
    memcpy(text, pkt.payload, pkt.payloadLen);
    text[pkt.payloadLen] = 0;
    String cmd = text;
    cmd.trim();
    for (const auto &c : kDmCommands) {
        if (!cmd.equalsIgnoreCase(c.name)) continue;
        String reply = c.run();
        uint8_t buf[240];
        size_t n = encodeTextMessageUnicast(reply.substring(0, 200).c_str(), s_nodeId, pkt.hdr.from, buf, sizeof buf);
        if (!n) return true;
        // Answered the way it was asked: a PKI DM gets a PKI reply. Its sender's
        // key is known, or the DM could not have been decrypted.
        const size_t airLen = pki ? n + 12 : n;
        bool ok = s_airtime.canSend(millis(), airMsFor(airLen), s_dutyLimitPct) &&
                  (pki ? radioSendDataPki(pkt.hdr.from, s_peerKeys.find(pkt.hdr.from), s_settings.maxHops, buf, n)
                       : radioSendData(pkt.hdr.from, slot, s_settings.maxHops, buf, n));
        if (ok) s_airtime.record(millis(), airMsFor(airLen));
        Serial.printf("[cs] %s DM from !%08x %s%d: reply %s\n", c.name, (unsigned)pkt.hdr.from,
                      pki ? "pki/ch" : "ch", slot, ok ? "ok" : "FAILED");
        return true;
    }
    return false;
}

// Our NODEINFO, public key included, sent to one node. At most once per node
// per 30 s: a node asking for it repeats itself every 5 s while its user keeps
// trying to send.
static void sendNodeInfoTo(uint32_t to, int slot, bool wantResponse, const char *why) {
    static uint32_t lastTo = 0, lastMs = 0;
    const uint32_t now = millis();
    if (to == lastTo && now - lastMs < 30000) return;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint8_t buf[160];
    size_t n = encodeNodeInfo(s_nodeId, s_settings.longName, s_settings.shortName, mac, buf, sizeof buf,
                              wantResponse);
    if (!n || !s_airtime.canSend(now, airMsFor(n), s_dutyLimitPct)) return;
    bool ok = radioSendData(to, slot, s_settings.maxHops, buf, n);
    if (ok) {
        s_airtime.record(now, airMsFor(n));
        lastTo = to;
        lastMs = now;
    }
    Serial.printf("[cs] nodeinfo to !%08x ch%d (%s): %s\n", (unsigned)to, slot, why, ok ? "sent" : "FAILED");
}

// A node that wants to DM us and has no key for us asks with a NODEINFO
// addressed here, want_response set (camillia-mt does this when a DM fails for
// want of a key). Answered on the same channel, so the key arrives without
// waiting up to three hours for the periodic broadcast.
static void answerNodeInfoRequest(const MeshPacket &pkt) {
    if (!pkt.wantResponse || pkt.hdr.to != s_nodeId) return;
    int slot = slotForPacket(pkt);
    if (slot != -2) sendNodeInfoTo(pkt.hdr.from, slot, false, "requested");
}

// A PKI DM we cannot open: its sender's key is not in s_peerKeys (it is kept in
// RAM, so a restart forgets them all). Ask the sender for its NODEINFO, on the
// primary as camillia-mt sends its own; its answer carries the key, and the
// next DM it sends gets through.
static void onPkiNoKey(uint32_t from) { sendNodeInfoTo(from, 0, true, "need its key"); }

static void ingest(const MeshPacket &pkt, MsgSource source) {
    if (pkt.portnum == NODEINFO_APP && pkt.hdr.from != s_nodeId) {
        UserInfo u;
        if (decodeUser(pkt.payload, pkt.payloadLen, u)) {
            s_names.update(pkt.hdr.from, u.longName, u.shortName, millis());
            if (u.hasPubKey) s_peerKeys.update(pkt.hdr.from, u.pubKey, millis());
        }
        if (source == SRC_LORA) answerNodeInfoRequest(pkt);
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
    boardEarlyInit();
    Serial.begin(115200);
    delay(1500);
    boardReport();
    s_nodeId = nodeIdFromMac();
    if (!settingsLoad(s_settings)) {
        settingsDefaults(s_settings, s_nodeId);
        settingsSave(s_settings);
        Serial.println("[cs] settings: defaults written");
    }
    Serial.printf("[cs] boot node !%08x \"%s\" (%s)\n", (unsigned)s_nodeId, s_settings.longName,
                  s_settings.shortName);

    s_names.begin(ps_malloc);   // no PSRAM: names just stay hex
    s_peerKeys.begin(ps_malloc);   // no PSRAM: no keys, so channel-key DMs only
    pkiBegin();
    displayBegin();
    batteryBegin();
    wifiBegin();
    configTzTime(s_settings.tz, "pool.ntp.org");
    cfgBegin();
    webBegin(&s_settings, [] { s_restartAtMs = millis() + 1500; }, statusJson, listMessages, clearMessages, storageJson);
    webSetAnnounce([] { return s_server.announceBroadcast(); });
    webSetPresets(communityPresetApply, communityPresetProblem);
    otaBegin(&s_settings);

    static_assert(CS_MAX_CHANNELS == SETTINGS_MAX_CHANNELS, "store and settings channel limits differ");
    // Only configured channels get a ring (~224 bytes of PSRAM per message).
    persistNoteFreePsram();
    for (int i = 0; i < s_settings.chanCount; i++) {
        if (!s_stores[i].begin(ps_malloc, randomU32, s_settings.msgsPerChannel))
            Serial.printf("[cs] store %d: no PSRAM\n", i);
    }
    persistBegin(s_settings.msgsPerChannel);
    persistLoadAll(s_stores, s_settings);

    radioSetPeerKeys([](uint32_t id) { return s_peerKeys.find(id); }, onPkiNoKey);
    radioBegin(s_settings, s_nodeId);
    mqttBegin(s_settings, s_nodeId, [](const MeshPacket &p) { ingest(p, SRC_MQTT); });
    serverBegin();
    identityBegin(s_settings, s_nodeId);
}

void loop() {
    const uint32_t nowMs = millis();
    webLoop();

    wifiLoop(nowMs);
    otaLoop(nowMs);
    // Give the web page a moment to show "installed" before the restart.
    if (otaRestartPending() && !s_restartAtMs) s_restartAtMs = nowMs + 3000;

    MeshPacket pkt;
    while (radioPoll(pkt)) {
        s_lastRxMs = millis();
        s_lastRssi = pkt.rssi;
        s_lastSnr = pkt.snr;
        s_haveLastRx = true;
        if (pkt.portnum == PORT_CHAT_SERVER) handleChatServerPacket(pkt, nowMs);
        else if (handleDmCommand(pkt)) {}
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
    st.chanCapacity = s_settings.msgsPerChannel;
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
