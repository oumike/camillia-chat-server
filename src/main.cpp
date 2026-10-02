// camillia chat server — setup and main loop wiring.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <time.h>
#include "channel_store.h"
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
    WiFi.softAP(ssid.c_str(), s_settings.apPass[0] ? s_settings.apPass : nullptr);
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
    webBegin(&s_settings, [] { s_restartAtMs = millis() + 1500; }, statusJson);

    for (int i = 0; i < CS_MAX_CHANNELS; i++) {
        if (!s_stores[i].begin(ps_malloc, randomU32)) Serial.printf("[cs] store %d: no PSRAM\n", i);
    }
    persistBegin();
    persistLoadAll(s_stores, s_settings);

    radioBegin(s_settings, s_nodeId);
    identityBegin(s_settings, s_nodeId);
}

void loop() {
    const uint32_t nowMs = millis();
    webLoop();

    MeshPacket pkt;
    while (radioPoll(pkt)) ingest(pkt);

    persistMaybeSave(s_stores, s_settings.chanCount, nowMs);
    identityLoop(nowMs);

    DisplayStatus st{};
    st.nodeName = s_settings.longName;
    snprintf(st.ip, sizeof st.ip, "%s", ipText().c_str());
    st.mqtt = "off";
    displayUpdate(st);

    if (s_restartAtMs && (int32_t)(nowMs - s_restartAtMs) >= 0) ESP.restart();
}
