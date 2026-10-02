// Task 1 smoke test: radio receive + OLED hello.
#include <Arduino.h>
#include <Wire.h>
#include <SSD1306Wire.h>
#include "board.h"
#include "mesh_radio.h"
#include "mesh_channel_plan.h"

// mesh_proto externs normally owned by camillia-mt's UI layer. PKI is unused.
uint8_t myPubKey[32]  = {0};
uint8_t myPrivKey[32] = {0};
uint8_t myDeviceRole  = 0;

static SSD1306Wire oled(OLED_ADDR, OLED_SDA, OLED_SCL);

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("[cs] boot");

    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, VEXT_ON_LEVEL);
    delay(50);
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);  delay(20);
    digitalWrite(OLED_RST, HIGH); delay(20);
    oled.init();
    Wire.beginTransmission(OLED_ADDR);
    Serial.printf("[cs] oled i2c 0x%02x %s\n", OLED_ADDR, Wire.endTransmission() == 0 ? "ack" : "NO ACK");
    oled.clear();
    oled.setFont(ArialMT_Plain_10);
    oled.drawString(0, 0, "Camillia Chat Server");
    oled.drawString(0, 14, "hello");
    oled.display();

    const PresetParams &p = kPresets[PRESET_LONG_FAST];
    float freq = regionSlotFreq("US", p.bw, p.channelName);
    bool ok = Radio.init() && Radio.reconfigure(freq, p.bw, p.sf, p.cr, MESH_POWER);
    Serial.printf("[cs] radio %s  %.3f MHz bw %.0f sf %u cr %u\n",
                  ok ? "ok" : "FAILED", freq, p.bw, p.sf, p.cr);
}

void loop() {
    MeshPacket pkt;
    if (Radio.pollRx(pkt)) {
        Serial.printf("[cs] rx from !%08x id %08x chan 0x%02x rssi %.0f snr %.1f\n",
                      (unsigned)pkt.hdr.from, (unsigned)pkt.hdr.id, pkt.hdr.channel,
                      pkt.rssi, pkt.snr);
    }
}
