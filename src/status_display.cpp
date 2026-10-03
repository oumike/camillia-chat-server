#include "status_display.h"
#include <Arduino.h>
#include <SSD1306Wire.h>
#include "board.h"

static SSD1306Wire s_oled(OLED_ADDR, OLED_SDA, OLED_SCL);
static uint32_t    s_lastDrawMs = 0;

void displayBegin() {
    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, VEXT_ON_LEVEL);
    delay(50);
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);  delay(20);
    digitalWrite(OLED_RST, HIGH); delay(20);
    s_oled.init();
    s_oled.flipScreenVertically();   // rotates 180° to suit how the board sits
    s_oled.setFont(ArialMT_Plain_10);
}

void displayUpdate(const DisplayStatus &st) {
    uint32_t now = millis();
    if (s_lastDrawMs && now - s_lastDrawMs < 1000) return;
    s_lastDrawMs = now;

    char stored[32];
    snprintf(stored, sizeof stored, "Stored: %d message%s", st.storedMessages,
             st.storedMessages == 1 ? "" : "s");
    char mqtt[32];
    snprintf(mqtt, sizeof mqtt, "MQTT: %s", st.mqtt);

    s_oled.clear();
    s_oled.drawString(0, 0, "Camillia Chat Server");
    s_oled.drawString(0, 12, st.nodeName);
    s_oled.drawString(0, 24, st.ip);
    s_oled.drawString(0, 36, mqtt);
    s_oled.drawString(0, 48, stored);
    s_oled.display();
}
