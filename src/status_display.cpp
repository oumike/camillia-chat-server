#include "status_display.h"
#include <Arduino.h>
#include <SSD1306Wire.h>
#include <time.h>
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
    s_oled.setFont(ArialMT_Plain_10);
}

void displayUpdate(const DisplayStatus &st) {
    uint32_t now = millis();
    if (s_lastDrawMs && now - s_lastDrawMs < 1000) return;
    s_lastDrawMs = now;

    char sent[32];
    if (!st.everSent) {
        strcpy(sent, "Sent: never");
    } else if (st.lastSentUnix) {
        time_t t = (time_t)st.lastSentUnix;
        struct tm tmv;
        localtime_r(&t, &tmv);
        strftime(sent, sizeof sent, "Sent: %Y-%m-%d %H:%M", &tmv);
    } else {
        uint32_t ago = now / 1000 - st.lastSentUptimeSec;
        if (ago < 3600) snprintf(sent, sizeof sent, "Sent: %um ago", (unsigned)(ago / 60));
        else snprintf(sent, sizeof sent, "Sent: %uh ago", (unsigned)(ago / 3600));
    }
    char mqtt[32];
    snprintf(mqtt, sizeof mqtt, "MQTT: %s", st.mqtt);

    s_oled.clear();
    s_oled.drawString(0, 0, "Camillia Chat Server");
    s_oled.drawString(0, 12, st.nodeName);
    s_oled.drawString(0, 24, st.ip);
    s_oled.drawString(0, 36, mqtt);
    s_oled.drawString(0, 48, sent);
    s_oled.display();
}
