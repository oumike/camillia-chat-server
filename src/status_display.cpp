#include "status_display.h"
#include <Arduino.h>
#include <SSD1306Wire.h>
#include "board.h"
#include <math.h>
#include "battery_level.h"

static SSD1306Wire s_oled(OLED_ADDR, OLED_SDA, OLED_SCL);
static uint32_t    s_lastDrawMs = 0;
static uint32_t    s_splashUntilMs = 0;
static constexpr uint32_t kSplashMs = 3000;

// camillia-mt's boot-splash camellia (drawBootSplash), same geometry, in 1 bit:
// every petal is filled white with a black edge, so each layer cuts visible
// separations into the one under it; the centre is black with white stamens.
static void drawCamellia(int cx, int cy, float scale) {
    auto S = [&](float v, int minV = 0) { int r = (int)lroundf(v * scale); return r < minV ? minV : r; };
    auto ring = [&](int n, float phase, int ox, int oy, int r0, int r1, bool edge) {
        for (int i = 0; i < n; i++) {
            float a = (float)i * 2.0f * (float)M_PI / (float)n + phase;
            int px = cx + (int)lroundf((float)ox * cosf(a));
            int py = cy + (int)lroundf((float)oy * sinf(a));
            int r = (i & 1) ? r1 : r0;
            s_oled.setColor(WHITE);
            s_oled.fillCircle(px, py, r);
            if (edge) { s_oled.setColor(BLACK); s_oled.drawCircle(px, py, r); }
        }
    };
    ring(10, 0.16f, S(23, 1), S(18, 1), S(11, 1), S(12, 1), true);   // outer petals
    ring(8, 0.42f, S(13, 1), S(10, 1), S(9, 1), S(9, 1), true);      // middle petals
    ring(5, 0.20f, S(6, 1), S(5, 1), S(6, 1), S(6, 1), true);        // inner petals

    s_oled.setColor(BLACK);
    s_oled.fillCircle(cx, cy, S(6, 1));                              // centre
    s_oled.setColor(WHITE);
    for (int i = 0; i < 10; i++) {                                   // stamens
        float a = (float)i * 2.0f * (float)M_PI / 10.0f;
        s_oled.setPixel(cx + (int)lroundf(S(4, 1) * cosf(a)), cy + (int)lroundf(S(4, 1) * sinf(a)));
    }

    s_oled.fillRect(cx - S(1), cy + S(20), S(3, 1), S(17, 1));       // stem
    const int sides[2][3] = {{-1, 28, 30}, {1, 29, 31}};
    for (const auto &sd : sides) {                                   // leaves with a vein
        s_oled.setColor(WHITE);
        s_oled.fillCircle(cx + sd[0] * S(21, 1), cy + S(sd[1], 1), S(8, 1));
        s_oled.setColor(BLACK);
        s_oled.drawCircle(cx + sd[0] * S(14, 1), cy + S(sd[2], 1), S(6, 1));
    }
    s_oled.setColor(WHITE);
}

static void drawSplash() {
    s_oled.clear();
    drawCamellia(31, 23, 0.72f);
    s_oled.setFont(ArialMT_Plain_16);
    s_oled.drawString(64, 6, "Camillia");
    s_oled.setFont(ArialMT_Plain_10);
    s_oled.drawString(64, 27, "Chat Server");
    s_oled.drawString(64, 45, "v" CS_VERSION);
    s_oled.display();
}

void displayBegin() {
    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, VEXT_ON_LEVEL);
    delay(50);
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);  delay(20);
    digitalWrite(OLED_RST, HIGH); delay(20);
    s_oled.init();
    s_oled.flipScreenVertically();   // rotates 180° to suit how the board sits
    drawSplash();
    s_splashUntilMs = millis() + kSplashMs;
    s_oled.setFont(ArialMT_Plain_10);
}

// Top-right battery: 13x7 outline + nub, filled by charge; a "+" when on USB
// power; nothing when no cell is connected.
static void drawBattery(const DisplayStatus &st) {
    if (st.battState == BATT_ABSENT) return;
    const int x = 113, y = 1, w = 13, h = 7;
    s_oled.setColor(WHITE);
    s_oled.drawRect(x, y, w, h);
    s_oled.fillRect(x + w, y + 2, 2, h - 4);
    if (st.battState == BATT_EXTERNAL) {
        s_oled.drawHorizontalLine(x + 4, y + 3, 5);
        s_oled.drawVerticalLine(x + 6, y + 1, 5);
        return;
    }
    int fill = (int)lroundf((w - 4) * st.battPct / 100.0f);
    if (fill < 1 && st.battPct > 0) fill = 1;
    if (fill > 0) s_oled.fillRect(x + 2, y + 2, fill, h - 4);
}

void displayUpdate(const DisplayStatus &st) {
    uint32_t now = millis();
    if ((int32_t)(now - s_splashUntilMs) < 0) return;
    if (s_lastDrawMs && now - s_lastDrawMs < 1000) return;
    s_lastDrawMs = now;

    char stored[32];
    snprintf(stored, sizeof stored, "Stored: %d message%s", st.storedMessages,
             st.storedMessages == 1 ? "" : "s");
    char mqtt[32];
    snprintf(mqtt, sizeof mqtt, "MQTT: %s", st.mqtt);

    s_oled.clear();
    const char *title = "Camillia Chat Server";
    if (st.battState != BATT_ABSENT && s_oled.getStringWidth(title) > 110) title = "Camillia CS";
    s_oled.drawString(0, 0, title);
    drawBattery(st);
    s_oled.drawString(0, 12, st.nodeName);
    s_oled.drawString(0, 24, st.ip);
    s_oled.drawString(0, 36, mqtt);
    s_oled.drawString(0, 48, stored);
    s_oled.display();
}
