// T-Deck Pro status page on the 3.1" GDEQ031T10 e-paper, 240x320 portrait.
// Panel class, pins, SPI clock and init sequence follow camillia-mt 3777e74
// src/hal/tdeck_pro_display.h (LGFX_TDeck::init / serviceRefresh), without
// LVGL: the page is drawn straight into GxEPD2's frame buffer.
//
// E-paper is slow and wears with use, so the page is redrawn only when its
// text changes, and then at most once a minute. Times and the battery are
// shown coarsely so an idle server settles to a refresh every few minutes.
// Partial refreshes, with a full one every kFullEvery to clear ghosting.
#include "status_display.h"
#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <math.h>
#include "board.h"
#include "battery_level.h"
#include "display_text.h"

using Panel = GxEPD2_310_GDEQ031T10;
static GxEPD2_BW<Panel, Panel::HEIGHT> s_epd(Panel(EINK_CS, EINK_DC, EINK_RST, EINK_BUSY));

static constexpr uint32_t kSplashMs = 3000;
static constexpr uint32_t kMinRefreshMs = 60000;
static constexpr uint8_t  kFullEvery = 10;
static constexpr int      kW = Panel::WIDTH;

static bool     s_ready = false;
static uint32_t s_splashUntilMs = 0;
static uint32_t s_lastRefreshMs = 0;
static uint32_t s_lastHash = 0;
static bool     s_drawnOnce = false;
static uint8_t  s_partials = 0;

static void parkBus() {
    digitalWrite(LORA_CS, HIGH);
    digitalWrite(SD_CS, HIGH);
    digitalWrite(EINK_CS, HIGH);
}

// Puts the frame buffer on the panel and powers the booster down again.
static void refresh(bool full) {
    parkBus();
    s_epd.display(!full);
    s_epd.powerOff();
    s_lastRefreshMs = millis();
    s_partials = full ? 0 : (uint8_t)(s_partials + 1);
}

// camillia-mt's boot-splash camellia, as status_display_oled.cpp draws it, in
// ink on paper: petals outlined, centre, stem and leaves solid.
static void drawCamellia(int cx, int cy, float scale) {
    auto S = [&](float v) { int r = (int)lroundf(v * scale); return r < 1 ? 1 : r; };
    auto ring = [&](int n, float phase, int ox, int oy, int r0, int r1) {
        for (int i = 0; i < n; i++) {
            float a = (float)i * 2.0f * (float)M_PI / (float)n + phase;
            int px = cx + (int)lroundf((float)ox * cosf(a));
            int py = cy + (int)lroundf((float)oy * sinf(a));
            int r = (i & 1) ? r1 : r0;
            s_epd.fillCircle(px, py, r, GxEPD_WHITE);
            s_epd.drawCircle(px, py, r, GxEPD_BLACK);
            s_epd.drawCircle(px, py, r - 1, GxEPD_BLACK);
        }
    };
    // Stem and leaves first so the outer petals overlap them.
    s_epd.fillRect(cx - S(1), cy + S(20), S(3), S(17), GxEPD_BLACK);
    const int sides[2][3] = {{-1, 28, 30}, {1, 29, 31}};
    for (const auto &sd : sides) {
        s_epd.fillCircle(cx + sd[0] * S(21), cy + S(sd[1]), S(8), GxEPD_BLACK);
        s_epd.drawCircle(cx + sd[0] * S(14), cy + S(sd[2]), S(6), GxEPD_WHITE);
    }
    ring(10, 0.16f, S(23), S(18), S(11), S(12));
    ring(8, 0.42f, S(13), S(10), S(9), S(9));
    ring(5, 0.20f, S(6), S(5), S(6), S(6));
    s_epd.fillCircle(cx, cy, S(6), GxEPD_BLACK);
    for (int i = 0; i < 10; i++) {
        float a = (float)i * 2.0f * (float)M_PI / 10.0f;
        s_epd.fillCircle(cx + (int)lroundf(S(4) * cosf(a)), cy + (int)lroundf(S(4) * sinf(a)), 1,
                         GxEPD_WHITE);
    }
}

static void drawCentered(const char *s, int baseline) {
    int16_t x1, y1;
    uint16_t w, h;
    s_epd.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    s_epd.setCursor((kW - (int)w) / 2 - x1, baseline);
    s_epd.print(s);
}

void displayBegin() {
    // Same bus as the radio: begin it with the pins here so GxEPD2's own
    // SPI.begin() (no pins) and mesh_radio's later one are no-ops.
    SPI.begin(EINK_SPI_SCK, EINK_SPI_MISO, EINK_SPI_MOSI, EINK_CS);
    parkBus();
    // 0: no GxEPD2 diagnostics on Serial (see camillia-mt's comment).
    s_epd.init(0, true, 2, false, SPI, SPISettings(EINK_SPI_HZ, MSBFIRST, SPI_MODE0));
    s_epd.setRotation(EINK_ROTATION);
    s_epd.setTextColor(GxEPD_BLACK);
    s_epd.setTextWrap(false);
    s_epd.setFullWindow();
    s_epd.fillScreen(GxEPD_WHITE);
    drawCamellia(kW / 2, 120, 2.2f);
    s_epd.setFont(&FreeSansBold18pt7b);
    drawCentered("Camillia", 240);
    s_epd.setFont(&FreeSans9pt7b);
    drawCentered("Chat Server", 266);
    drawCentered("v" CS_VERSION, 292);
    refresh(true);
    s_ready = true;
    s_splashUntilMs = millis() + kSplashMs;
    Serial.printf("[eink] GDEQ031T10 ready (%dx%d)\n", s_epd.width(), s_epd.height());
}

// Coarse on purpose: every change in these strings costs a panel refresh.
static void coarseAge(uint32_t sec, char *out, size_t cap) {
    if (sec < 600) snprintf(out, cap, "<10m");
    else if (sec < 3600) snprintf(out, cap, "%um", (unsigned)(sec / 600 * 10));
    else if (sec < 172800) snprintf(out, cap, "%uh", (unsigned)(sec / 3600));
    else snprintf(out, cap, "%ud", (unsigned)(sec / 86400));
}

static void textFit(const char *in, char *out, size_t cap, int maxW) {
    oneLine(in, out, cap);
    int16_t x1, y1;
    uint16_t w, h;
    size_t n = strlen(out);
    for (;;) {
        s_epd.getTextBounds(out, 0, 0, &x1, &y1, &w, &h);
        if ((int)w <= maxW || n == 0) return;
        // Step back over a whole UTF-8 sequence.
        do { out[--n] = '\0'; } while (n > 0 && ((uint8_t)out[n] & 0xC0) == 0x80);
    }
}

static void drawBattery(const DisplayStatus &st, int x, int y) {
    const int w = 22, h = 11;
    s_epd.drawRect(x, y, w, h, GxEPD_WHITE);
    s_epd.fillRect(x + w, y + 3, 2, h - 6, GxEPD_WHITE);
    if (st.battState == BATT_ABSENT) {
        s_epd.drawFastHLine(x + 7, y + 5, 8, GxEPD_WHITE);
    } else if (st.battState == BATT_EXTERNAL) {
        s_epd.drawFastHLine(x + 7, y + 5, 8, GxEPD_WHITE);
        s_epd.drawFastVLine(x + 11, y + 2, 7, GxEPD_WHITE);
    } else {
        int fill = (int)lroundf((w - 4) * st.battPct / 100.0f);
        if (fill < 1 && st.battPct > 0) fill = 1;
        if (fill > 0) s_epd.fillRect(x + 2, y + 2, fill, h - 4, GxEPD_WHITE);
    }
}

static uint32_t fnv(uint32_t h, const char *s) {
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return (h ^ 0xFF) * 16777619u;
}

void displayUpdate(const DisplayStatus &st) {
    if (!s_ready) return;
    uint32_t now = millis();
    if ((int32_t)(now - s_splashUntilMs) < 0) return;
    if (s_drawnOnce && now - s_lastRefreshMs < kMinRefreshMs) return;

    // Text of every line, built first so an unchanged page costs no refresh.
    static char name[48], ids[40], wifi[40], mqtt[40], stored[40], rx[40], sig[40], air[40],
        batt[24], up[48];
    static char chans[CS_MAX_CHANNELS][32];
    s_epd.setFont(&FreeSansBold9pt7b);
    textFit(st.nodeName ? st.nodeName : "", name, sizeof name, kW - 8);
    s_epd.setFont(&FreeSans9pt7b);
    snprintf(ids, sizeof ids, "!%08x   v%s", (unsigned)st.nodeId, CS_VERSION);
    snprintf(wifi, sizeof wifi, "IP %s", st.ip[0] ? st.ip : "--");
    snprintf(mqtt, sizeof mqtt, "MQTT: %s", st.mqtt ? st.mqtt : "--");
    snprintf(stored, sizeof stored, "Stored: %d msg%s, %d ch", st.storedMessages,
             st.storedMessages == 1 ? "" : "s", st.chanCount);
    int shownCh = 0;
    for (int i = 0; i < st.chanCount && i < CS_MAX_CHANNELS; i++) {
        const char *cn = st.chanName ? st.chanName(i) : nullptr;
        char raw[32];
        snprintf(raw, sizeof raw, "%s %d", cn && cn[0] ? cn : "?", st.chanCounts[i]);
        textFit(raw, chans[i], sizeof chans[i], kW / 2 - 8);
        shownCh++;
    }
    char age[12];
    if (st.haveLastRx) {
        coarseAge(st.lastRxAgeSec, age, sizeof age);
        snprintf(rx, sizeof rx, "Last RX: %s ago", age);
        snprintf(sig, sizeof sig, "RSSI %d  SNR %d", (int)lroundf(st.lastRssi), (int)lroundf(st.lastSnr));
    } else {
        snprintf(rx, sizeof rx, "Last RX: none yet");
        sig[0] = '\0';
    }
    snprintf(air, sizeof air, "Airtime %d%% (limit %d%%)", (int)lroundf(st.airtimePct), st.dutyLimitPct);
    if (st.battState == BATT_ABSENT) snprintf(batt, sizeof batt, "No battery");
    else if (st.battState == BATT_EXTERNAL) snprintf(batt, sizeof batt, "USB power");
    else snprintf(batt, sizeof batt, "Battery %d%%", (st.battPct + 2) / 5 * 5);
    coarseAge(st.uptimeSec, age, sizeof age);
    snprintf(up, sizeof up, "%s   Up %s", batt, age);

    uint32_t h = 2166136261u;
    for (const char *s : {name, ids, wifi, mqtt, stored, rx, sig, air, up}) h = fnv(h, s);
    for (int i = 0; i < shownCh; i++) h = fnv(h, chans[i]);
    h = fnv(h, st.battState == BATT_PRESENT ? "p" : st.battState == BATT_EXTERNAL ? "e" : "a");
    if (s_drawnOnce && h == s_lastHash) return;
    s_lastHash = h;

    s_epd.setFullWindow();
    s_epd.fillScreen(GxEPD_WHITE);

    // Title bar, inverted.
    s_epd.fillRect(0, 0, kW, 22, GxEPD_BLACK);
    s_epd.setTextColor(GxEPD_WHITE);
    s_epd.setFont(&FreeSansBold9pt7b);
    s_epd.setCursor(4, 16);
    s_epd.print("Camillia Chat Server");
    drawBattery(st, kW - 30, 6);
    s_epd.setTextColor(GxEPD_BLACK);

    int y = 42;
    s_epd.setCursor(4, y);
    s_epd.print(name);
    s_epd.setFont(&FreeSans9pt7b);
    auto line = [&](const char *s) {
        y += 18;
        s_epd.setCursor(4, y);
        s_epd.print(s);
    };
    auto rule = [&]() {
        y += 6;
        s_epd.drawFastHLine(0, y, kW, GxEPD_BLACK);
        y += 2;
    };
    line(ids);
    rule();
    line(wifi);
    line(mqtt);
    rule();
    line(stored);
    // Channels in two columns; whatever does not fit becomes "+N more".
    constexpr int kChanRows = 5;
    int cells = shownCh <= kChanRows * 2 ? shownCh : kChanRows * 2 - 1;
    int rows = (cells + 1) / 2;
    if (shownCh > cells) rows = kChanRows;
    for (int i = 0; i < cells; i++) {
        s_epd.setCursor(4 + (i / rows) * (kW / 2), y + 18 * (1 + i % rows));
        s_epd.print(chans[i]);
    }
    if (shownCh > cells) {
        char more[16];
        snprintf(more, sizeof more, "+%d more", shownCh - cells);
        s_epd.setCursor(4 + kW / 2, y + 18 * kChanRows);
        s_epd.print(more);
    }
    y += 18 * rows;
    rule();
    line(rx);
    if (sig[0]) line(sig);
    line(air);
    line(up);

    refresh(!s_drawnOnce || s_partials >= kFullEvery);
    s_drawnOnce = true;
}

// No logo on this display: cfg caps leaves it out.
bool displayLogoBox(uint16_t &, uint16_t &) { return false; }
