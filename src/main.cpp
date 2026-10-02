// camillia chat server — setup and main loop wiring.
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <SSD1306Wire.h>
#include <esp_mac.h>
#include <time.h>
#include "board.h"
#include "mesh_radio.h"
#include "mesh_channel_plan.h"
#include "settings.h"
#include "web_ui.h"

// mesh_proto externs normally owned by camillia-mt's UI layer. PKI is unused.
uint8_t myPubKey[32]  = {0};
uint8_t myPrivKey[32] = {0};
uint8_t myDeviceRole  = 0;

static SSD1306Wire s_oled(OLED_ADDR, OLED_SDA, OLED_SCL);
static Settings    s_settings;
static uint32_t    s_nodeId = 0;
static uint32_t    s_restartAtMs = 0;
static bool        s_staStarted = false;

static uint32_t nodeIdFromMac() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    return (uint32_t)mac[2] << 24 | (uint32_t)mac[3] << 16 | (uint32_t)mac[4] << 8 | mac[5];
}

static String ipText() {
    if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
    return "AP " + WiFi.softAPIP().toString();
}

static void wifiBegin() {
    WiFi.mode(s_settings.staSsid[0] ? WIFI_AP_STA : WIFI_AP);
    String ssid = String("camillia-cs-") + s_settings.shortName;
    WiFi.softAP(ssid.c_str(), s_settings.apPass[0] ? s_settings.apPass : nullptr);
    if (s_settings.staSsid[0]) {
        WiFi.begin(s_settings.staSsid, s_settings.staPass);
        s_staStarted = true;
    }
    Serial.printf("[cs] AP %s at %s\n", ssid.c_str(), WiFi.softAPIP().toString().c_str());
}

static String statusJson() {
    String j = "{";
    j += "\"node\":\"" + String(s_settings.longName) + "\",";
    j += "\"nodeId\":\"!" + String(s_nodeId, HEX) + "\",";
    j += "\"ip\":\"" + ipText() + "\",";
    j += "\"uptimeSec\":" + String(millis() / 1000);
    j += "}";
    return j;
}

static void oledBegin() {
    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, VEXT_ON_LEVEL);
    delay(50);
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);  delay(20);
    digitalWrite(OLED_RST, HIGH); delay(20);
    s_oled.init();
    s_oled.setFont(ArialMT_Plain_10);
}

static void oledDraw() {
    s_oled.clear();
    s_oled.drawString(0, 0, "Camillia Chat Server");
    s_oled.drawString(0, 13, s_settings.longName);
    s_oled.drawString(0, 26, ipText());
    s_oled.display();
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

    oledBegin();
    wifiBegin();
    configTime(0, 0, "pool.ntp.org");
    webBegin(&s_settings, [] { s_restartAtMs = millis() + 1500; }, statusJson);

    const PresetParams &p = kPresets[s_settings.modemPreset < PRESET_COUNT ? s_settings.modemPreset : 0];
    float freq = s_settings.freqSlot
                     ? regionSlotFreqNum(s_settings.region, p.bw, s_settings.freqSlot - 1)
                     : regionSlotFreq(s_settings.region, p.bw, p.channelName);
    bool ok = Radio.init() && Radio.reconfigure(freq, p.bw, p.sf, p.cr, regionPower(s_settings.region));
    Serial.printf("[cs] radio %s %s %.3f MHz\n", ok ? "ok" : "FAILED", p.name, freq);
}

void loop() {
    webLoop();
    MeshPacket pkt;
    if (Radio.pollRx(pkt)) {
        Serial.printf("[cs] rx from !%08x id %08x chan 0x%02x\n", (unsigned)pkt.hdr.from,
                      (unsigned)pkt.hdr.id, pkt.hdr.channel);
    }
    static uint32_t lastDraw = 0;
    if (millis() - lastDraw > 1000) { lastDraw = millis(); oledDraw(); }
    if (s_restartAtMs && (int32_t)(millis() - s_restartAtMs) >= 0) ESP.restart();
}
