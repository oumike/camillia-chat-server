#include "node_identity.h"
#include <Arduino.h>
#include <esp_mac.h>
#include "mesh_proto.h"
#include "radio_link.h"

static const Settings *s_cfg = nullptr;
static uint32_t s_nodeId = 0;
static uint32_t s_nextMs = 0;
static int      s_nextSlot = 0;
static constexpr uint32_t kFirstDelayMs = 15000;           // let the radio settle after boot
static constexpr uint32_t kIntervalMs   = 3UL * 3600 * 1000;
static constexpr uint32_t kSpacingMs    = 5000;            // between channels

void identityBegin(const Settings &s, uint32_t myNodeId) {
    s_cfg = &s;
    s_nodeId = myNodeId;
    s_nextMs = millis() + kFirstDelayMs;
    s_nextSlot = 0;
}

void identityLoop(uint32_t nowMs) {
    if (!s_cfg || (int32_t)(nowMs - s_nextMs) < 0) return;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint8_t data[160];
    size_t n = encodeNodeInfo(s_nodeId, s_cfg->longName, s_cfg->shortName, mac, data, sizeof data,
                              /*wantResponse=*/false);
    bool ok = n && radioSendData(0xFFFFFFFF, s_nextSlot, 3, data, n);
    Serial.printf("[cs] nodeinfo on channel %d %s\n", s_nextSlot, ok ? "sent" : "FAILED");
    if (++s_nextSlot >= s_cfg->chanCount) {
        s_nextSlot = 0;
        s_nextMs = nowMs + kIntervalMs;
    } else {
        s_nextMs = nowMs + kSpacingMs;
    }
}
