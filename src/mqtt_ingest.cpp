#include "mqtt_ingest.h"
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <string.h>

static const Settings *s_cfg = nullptr;
static WiFiClient      s_net;
static PubSubClient    s_mqtt(s_net);
static std::function<void(const MeshPacket &)> s_onPacket;
static char     s_clientId[32];
static uint32_t s_nextTryMs = 0;
static uint32_t s_backoffMs = 5000;
static constexpr uint32_t kMaxBackoffMs = 300000;
// "undecodable" is mostly payload-less packets brokers replay as retained messages.
static uint32_t s_rxCount = 0, s_decCount = 0, s_undecodable = 0, s_pki = 0, s_noKey = 0;
static char     s_lastNoKeyChan[16] = "";

static void onMessage(char *topic, uint8_t *payload, unsigned int len) {
    s_rxCount++;
    MeshPacket pkt{};
    uint8_t cipher[240];
    size_t cipherLen = 0;
    char chan[16];
    if (!decodeServiceEnvelope(payload, len, pkt.hdr, cipher, sizeof cipher, cipherLen, chan, sizeof chan)) {
        s_undecodable++;
        return;
    }
    if (pkt.hdr.channel == 0 || cipherLen == 0) { s_pki++; return; }   // PKI / empty: not channel traffic

    uint8_t plain[256];
    pkt.chanIdx = decryptPacket(pkt.hdr, cipher, plain, cipherLen);
    if (pkt.chanIdx < 0) {
        s_noKey++;
        strncpy(s_lastNoKeyChan, chan, sizeof s_lastNoKeyChan - 1);
        return;
    }
    s_decCount++;
    const uint8_t *pay = nullptr;
    size_t payLen = 0;
    if (!decodeData(plain, cipherLen, pkt.portnum, pay, payLen, pkt.requestId, pkt.wantResponse) || !pay ||
        payLen > sizeof pkt.payload)
        return;
    memcpy(pkt.payload, pay, payLen);
    pkt.payloadLen = payLen;
    pkt.decrypted = true;
    pkt.rxMs = millis();
    if (s_onPacket) s_onPacket(pkt);
}

void mqttBegin(const Settings &s, uint32_t myNodeId, std::function<void(const MeshPacket &)> onPacket) {
    s_cfg = &s;
    s_onPacket = onPacket;
    snprintf(s_clientId, sizeof s_clientId, "camillia-cs-%08x", (unsigned)myNodeId);
    if (!s.mqttEnabled) return;
    s_mqtt.setServer(s.mqttHost, s.mqttPort);
    s_mqtt.setCallback(onMessage);
    s_mqtt.setBufferSize(1024);
    s_mqtt.setSocketTimeout(5);   // PubSubClient's connect blocks; keep the stall short
    s_net.setTimeout(5);
}

static void connect(uint32_t nowMs) {
    Serial.printf("[cs] mqtt connecting to %s:%u\n", s_cfg->mqttHost, s_cfg->mqttPort);
    bool ok = s_mqtt.connect(s_clientId, s_cfg->mqttUser[0] ? s_cfg->mqttUser : nullptr,
                             s_cfg->mqttUser[0] ? s_cfg->mqttPass : nullptr);
    if (!ok) {
        Serial.printf("[cs] mqtt connect failed (state %d), retry in %lus\n", s_mqtt.state(),
                      (unsigned long)(s_backoffMs / 1000));
        s_nextTryMs = nowMs + s_backoffMs;
        s_backoffMs = s_backoffMs * 2 > kMaxBackoffMs ? kMaxBackoffMs : s_backoffMs * 2;
        return;
    }
    s_backoffMs = 5000;
    for (int i = 0; i < s_cfg->chanCount; i++) {
        char topic[128];
        snprintf(topic, sizeof topic, "%s/2/e/%s/#", s_cfg->mqttRoot, s_cfg->ch[i].name);
        bool sub = s_mqtt.subscribe(topic);
        Serial.printf("[cs] mqtt subscribe %s %s\n", topic, sub ? "ok" : "FAILED");
    }
}

void mqttLoop(uint32_t nowMs) {
    if (!s_cfg || !s_cfg->mqttEnabled) return;
    if (WiFi.status() != WL_CONNECTED) return;
    if (s_mqtt.connected()) {
        s_mqtt.loop();
        return;
    }
    if ((int32_t)(nowMs - s_nextTryMs) >= 0) connect(nowMs);
}

String mqttDiagJson() {
    return String("{\"undecodable\":") + s_undecodable + ",\"pki\":" + s_pki + ",\"noKey\":" + s_noKey +
           ",\"lastNoKeyChannel\":\"" + s_lastNoKeyChan + "\"}";
}

void mqttCounters(uint32_t &received, uint32_t &decrypted) {
    received = s_rxCount;
    decrypted = s_decCount;
}

const char *mqttState() {
    if (!s_cfg || !s_cfg->mqttEnabled) return "off";
    if (WiFi.status() != WL_CONNECTED) return "no wifi";
    return s_mqtt.connected() ? "connected" : "connecting";
}
