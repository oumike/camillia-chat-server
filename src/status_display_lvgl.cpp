// Status display for the 320x240 TFT builds: LVGL 9.5 (spec §6). Panel,
// backlight, touch and the Wake button are behind display_hal.h, one file per
// board. The plain V4 builds status_display_oled.cpp instead (platformio.ini
// build_src_filter).
#include "status_display.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <lvgl.h>
#include "battery_level.h"
#include "board.h"
#include "cs_server.h"
#include "display_hal.h"
#include "display_text.h"
#include "mesh_channel_plan.h"
#include "page_cycler.h"

namespace {

constexpr int32_t  kW = TFT_LANDSCAPE_W, kH = TFT_LANDSCAPE_H;
constexpr int32_t  kBarH = 20;
constexpr int32_t  kBufLines = 40;
constexpr uint32_t kSplashMs = 3000;
constexpr int      kFeedRows = 7, kActRows = 11, kChanRows = 5;

// camillia-mt's "Camillia Dark" theme preset (kUiThemePresets[0]) and splash text colours.
constexpr uint16_t kBgMain = 0x0843, kPanelBg = 0x1065, kPanelAlt = 0x18A7, kAccent = 0xDA8E;
constexpr uint32_t kTextMain = 0xF3F6FA, kTextDim = 0xB7C0CC;

bool          s_off = false;
lv_display_t *s_disp = nullptr;
uint8_t      *s_buf1 = nullptr, *s_buf2 = nullptr;

lv_obj_t      *s_splash = nullptr;
lv_draw_buf_t *s_splashBuf = nullptr;
uint32_t       s_splashUntilMs = 0;

lv_obj_t *s_main = nullptr;
lv_obj_t *s_barRight = nullptr;
lv_obj_t *s_dots[3];
lv_obj_t *s_page[3];
lv_obj_t *s_p1[8];                       // page 1 single rows
lv_obj_t *s_p1Chan[kChanRows * 2];       // page 1 store rows, two columns
lv_obj_t *s_msgHead[kFeedRows], *s_msgText[kFeedRows], *s_msgEmpty = nullptr;
lv_obj_t *s_act[kActRows], *s_actEmpty = nullptr;

PageCycler s_cycler;
bool       s_started = false;    // splash over, cycler running
int        s_shownPage = -1;
uint8_t    s_blLevel = TFT_BRIGHTNESS_DEFAULT;
uint32_t   s_lastFillMs = 0;

StoredMsg     s_msgs[kFeedRows];
int8_t        s_msgSlots[kFeedRows];
ActivityEntry s_acts[kActRows];

// ── Colour helpers (blend565 is camillia-mt's) ───────────────────────────────
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t blend565(uint16_t c1, uint16_t c2, uint8_t t) {
    int r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
    int r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
    int r = r1 + ((r2 - r1) * t) / 255;
    int g = g1 + ((g2 - g1) * t) / 255;
    int b = b1 + ((b2 - b1) * t) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

lv_color_t c565(uint16_t c) {
    const uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    return lv_color_make((uint8_t)((r << 3) | (r >> 2)), (uint8_t)((g << 2) | (g >> 4)),
                         (uint8_t)((b << 3) | (b >> 2)));
}

void fail(const char *reason) {
    Serial.printf("[cs] display: %s\n", reason);
    dhalOff();   // don't leave a lit black panel
    s_off = true;
}

uint32_t tickMs() { return millis(); }

// ── LVGL glue (camillia-mt lvglFlush / lvglTouchRead) ───────────────────────
void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;
    dhalFlush(area->x1, area->y1, w, h, (const uint16_t *)px_map);
    lv_display_flush_ready(disp);
}

// Touch-failure handling lives in the back end: dhalTouch() is false once touch is off.
void touchReadCb(lv_indev_t *indev, lv_indev_data_t *data) {
    LV_UNUSED(indev);
    data->state = LV_INDEV_STATE_RELEASED;
    int16_t tx = 0, ty = 0;
    if (dhalTouch(tx, ty)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = tx;
        data->point.y = ty;
    }
}

void onTap(lv_event_t *e) {
    LV_UNUSED(e);
    if (s_started) s_cycler.tap(millis());
}

// ── Object helpers ───────────────────────────────────────────────────────────
// Plain container: unstyled, not clickable (taps fall through to the screen),
// not scrollable.
lv_obj_t *mkBox(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    return o;
}

lv_obj_t *mkText(lv_obj_t *parent, const lv_font_t *font, lv_color_t col, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_label_set_text(l, text);
    return l;
}

// One fixed-size line, cut with "..." by LVGL when too wide.
lv_obj_t *mkRow(lv_obj_t *parent, const lv_font_t *font, lv_color_t col, int32_t x, int32_t y, int32_t w) {
    lv_obj_t *l = mkText(parent, font, col, "");
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, lv_font_get_line_height(font));
    return l;
}

void setText(lv_obj_t *l, const char *s) {
    const char *cur = lv_label_get_text(l);
    if (!cur || strcmp(cur, s) != 0) lv_label_set_text(l, s);
}

void setVisible(lv_obj_t *o, bool on) {
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// ── Splash: camillia-mt drawBootSplash, colour camellia on an lv_canvas ─────
void px(lv_draw_buf_t *b, int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= (int)b->header.w || y >= (int)b->header.h) return;
    ((uint16_t *)(b->data + (size_t)y * b->header.stride))[x] = c;
}

// Disc (fill) or one-pixel ring (edge), close to LovyanGFX fillCircle/drawCircle.
void circle(lv_draw_buf_t *b, int cx, int cy, int r, uint16_t c, bool ringOnly) {
    const int outer = r * r + r, inner = r * r - r;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            const int d = dx * dx + dy * dy;
            if (d <= outer && (!ringOnly || d > inner)) px(b, cx + dx, cy + dy, c);
        }
}

// drawCamelliaMark at scale 1 (camillia-mt's flowerScale for this board), alpha 255.
void drawCamellia(lv_draw_buf_t *b, int cx, int cy) {
    const uint16_t PETAL_OUTER = 0xF9CF, PETAL_MID = 0xFADF, PETAL_INNER = 0xFF7D;
    const uint16_t PETAL_EDGE = 0xD8A7, CENTER = 0xFD20, CENTER_DOT = 0xFEA0;
    const uint16_t STEM = 0x64EC, LEAF_DARK = 0x2C87, LEAF_LIGHT = 0x3D68;
    auto ring = [&](int n, float phase, int ox, int oy, int r0, int r1, uint16_t col, bool edge) {
        for (int i = 0; i < n; i++) {
            const float a = ((float)i * 2.0f * (float)M_PI / (float)n) + phase;
            const int x = cx + (int)lroundf((float)ox * cosf(a));
            const int y = cy + (int)lroundf((float)oy * sinf(a));
            const int r = (i & 1) ? r1 : r0;
            circle(b, x, y, r, col, false);
            if (edge) circle(b, x, y, r, PETAL_EDGE, true);
        }
    };
    ring(10, 0.16f, 23, 18, 11, 12, PETAL_OUTER, true);
    ring(8, 0.42f, 13, 10, 9, 9, PETAL_MID, true);
    ring(5, 0.20f, 6, 5, 6, 6, PETAL_INNER, false);
    circle(b, cx, cy, 6, CENTER, false);
    circle(b, cx, cy, 6, 0xD4C0, true);
    for (int i = 0; i < 10; i++) {
        const float a = (float)i * 2.0f * (float)M_PI / 10.0f;
        circle(b, cx + (int)lroundf(4.0f * cosf(a)), cy + (int)lroundf(4.0f * sinf(a)), 1, CENTER_DOT, false);
    }
    for (int y = 0; y < 17; y++)                    // stem: 3x17 round rect, radius 1
        for (int x = 0; x < 3; x++)
            if (!((x == 0 || x == 2) && (y == 0 || y == 16))) px(b, cx - 1 + x, cy + 20 + y, STEM);
    circle(b, cx - 21, cy + 28, 8, LEAF_DARK, false);
    circle(b, cx - 14, cy + 30, 6, LEAF_LIGHT, false);
    circle(b, cx + 21, cy + 29, 8, LEAF_DARK, false);
    circle(b, cx + 14, cy + 31, 6, LEAF_LIGHT, false);
}

void buildSplash() {
    const uint16_t bgTop = blend565(kBgMain, kPanelBg, 96);
    const uint16_t cardEdge = blend565(kPanelBg, kAccent, 66);
    const uint16_t cardEdgeHi = blend565(kPanelAlt, kAccent, 92);
    const uint16_t dimCol = blend565(rgb565(0xB7, 0xC0, 0xCC), kPanelBg, 72);

    s_splash = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_splash);
    lv_obj_remove_flag(s_splash, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_splash, c565(bgTop), 0);
    lv_obj_set_style_bg_grad_color(s_splash, c565(kBgMain), 0);
    lv_obj_set_style_bg_grad_dir(s_splash, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_splash, LV_OPA_COVER, 0);

    // Card: 10 px margin, radius 12, two edge lines (cardEdge, cardEdgeHi inset 1).
    lv_obj_t *card = mkBox(s_splash, 10, 10, kW - 20, kH - 20);
    lv_obj_set_style_bg_color(card, c565(kPanelBg), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, c565(cardEdge), 0);
    lv_obj_t *edge = mkBox(s_splash, 11, 11, kW - 22, kH - 22);
    lv_obj_set_style_radius(edge, 12, 0);
    lv_obj_set_style_border_width(edge, 1, 0);
    lv_obj_set_style_border_color(edge, c565(cardEdgeHi), 0);

    lv_obj_align(mkText(s_splash, &lv_font_montserrat_20, lv_color_hex(kTextMain), "Camillia"),
                 LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_align(mkText(s_splash, &lv_font_montserrat_16, lv_color_hex(kTextMain), "Chat Server"),
                 LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_align(mkText(s_splash, &lv_font_montserrat_12, c565(dimCol), "v" CS_VERSION),
                 LV_ALIGN_TOP_MID, 0, 64);

    // Flower centred between the title block (~y 81) and cardY + cardH - 40,
    // as in drawBootSplash. 80x76 canvas, mark centre at (40, 32).
    s_splashBuf = lv_draw_buf_create(80, 76, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    if (!s_splashBuf) return;   // splash without the flower
    lv_obj_t *canvas = lv_canvas_create(s_splash);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_set_draw_buf(canvas, s_splashBuf);
    lv_canvas_fill_bg(canvas, c565(kPanelBg), LV_OPA_COVER);
    drawCamellia(s_splashBuf, 40, 32);
    lv_obj_set_pos(canvas, kW / 2 - 40, (81 + (kH - 10 - 40)) / 2 - 32);
    lv_obj_invalidate(canvas);
}

void endSplash() {
    lv_screen_load(s_main);
    if (s_splash) lv_obj_delete(s_splash);   // deletes the canvas before its buffer goes
    s_splash = nullptr;
    if (s_splashBuf) lv_draw_buf_destroy(s_splashBuf);
    s_splashBuf = nullptr;
}

// ── Main screen ──────────────────────────────────────────────────────────────
void buildMain() {
    const lv_color_t text = lv_color_hex(kTextMain), dim = lv_color_hex(kTextDim), accent = c565(kAccent);
    s_main = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_main);
    lv_obj_remove_flag(s_main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_main, c565(kBgMain), 0);
    lv_obj_set_style_bg_opa(s_main, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_main, onTap, LV_EVENT_CLICKED, nullptr);

    // Status bar: title left; clock + battery, then three page dots, right.
    lv_obj_t *bar = mkBox(s_main, 0, 0, kW, kBarH);
    lv_obj_set_style_bg_color(bar, c565(kPanelBg), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_t *title = mkRow(bar, &lv_font_montserrat_14, text, 4, 2, 170);
    lv_label_set_text(title, "Camillia Chat Server");
    s_barRight = mkRow(bar, &lv_font_montserrat_12, text, 176, 3, 110);
    lv_obj_set_style_text_align(s_barRight, LV_TEXT_ALIGN_RIGHT, 0);
    for (int i = 0; i < 3; i++) {
        s_dots[i] = mkBox(bar, 292 + i * 9, 7, 6, 6);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_dots[i], dim, 0);
    }

    for (int i = 0; i < 3; i++) {
        s_page[i] = mkBox(s_main, 0, kBarH, kW, kH - kBarH);
        setVisible(s_page[i], false);
    }

    // Page 1: health. 13 rows of 16 px; rows 6-10 are the store, two columns.
    const lv_font_t *f12 = &lv_font_montserrat_12;
    auto rowY = [](int row) { return (int32_t)(3 + row * 16); };
    for (int i = 0; i < 6; i++) s_p1[i] = mkRow(s_page[0], f12, i == 0 ? accent : text, 6, rowY(i), 308);
    for (int i = 0; i < kChanRows * 2; i++)
        s_p1Chan[i] = mkRow(s_page[0], f12, dim, (i % 2) ? 163 : 6, rowY(6 + i / 2), 151);
    s_p1[6] = mkRow(s_page[0], f12, text, 6, rowY(11), 308);
    s_p1[7] = mkRow(s_page[0], f12, text, 6, rowY(12), 308);

    // Page 2: message feed, two lines per message (15 + 16 px).
    for (int i = 0; i < kFeedRows; i++) {
        s_msgHead[i] = mkRow(s_page[1], f12, accent, 6, 3 + i * 31, 308);
        s_msgText[i] = mkRow(s_page[1], &lv_font_montserrat_14, text, 6, 3 + i * 31 + 15, 308);
    }
    s_msgEmpty = mkText(s_page[1], &lv_font_montserrat_16, dim, "No messages yet");
    lv_obj_align(s_msgEmpty, LV_ALIGN_CENTER, 0, 0);

    // Page 3: sync activity, 20 px rows.
    for (int i = 0; i < kActRows; i++) s_act[i] = mkRow(s_page[2], &lv_font_montserrat_14, text, 6, 1 + i * 20, 308);
    s_actEmpty = mkText(s_page[2], &lv_font_montserrat_16, dim, "No sync activity yet");
    lv_obj_align(s_actEmpty, LV_ALIGN_CENTER, 0, 0);
}

// ── Fill ─────────────────────────────────────────────────────────────────────
void fillBar(const DisplayStatus &st) {
    char clock[8] = "";
    if (st.clockSet) {
        time_t t = time(nullptr);
        struct tm tm;
        localtime_r(&t, &tm);
        snprintf(clock, sizeof clock, "%02d:%02d", tm.tm_hour, tm.tm_min);
    }
    char batt[16] = "";
    if (st.battState == BATT_EXTERNAL) {
        snprintf(batt, sizeof batt, "%s", LV_SYMBOL_CHARGE);
    } else if (st.battState != BATT_ABSENT) {
        const char *icon = st.battPct >= 90 ? LV_SYMBOL_BATTERY_FULL : st.battPct >= 65 ? LV_SYMBOL_BATTERY_3
                         : st.battPct >= 40 ? LV_SYMBOL_BATTERY_2 : st.battPct >= 15 ? LV_SYMBOL_BATTERY_1
                         : LV_SYMBOL_BATTERY_EMPTY;
        snprintf(batt, sizeof batt, "%s %u%%", icon, (unsigned)st.battPct);
    }
    char buf[32];
    snprintf(buf, sizeof buf, "%s%s%s", clock, clock[0] && batt[0] ? "  " : "", batt);
    setText(s_barRight, buf);
}

void nameOf(const DisplayStatus &st, uint32_t id, char *out, size_t cap) {
    if (st.names) st.names->displayName(id, out, cap);
    else snprintf(out, cap, "!%08x", (unsigned)id);
}

const char *chanOf(const DisplayStatus &st, int slot) {
    return st.chanName ? st.chanName(slot) : nullptr;
}

void fillHealth(const DisplayStatus &st) {
    const Settings *s = st.settings;
    char buf[96], t1[16];
    snprintf(buf, sizeof buf, "%s  !%08x  (%s)", s ? s->longName : "", (unsigned)st.nodeId, s ? s->shortName : "");
    setText(s_p1[0], buf);
    const char *preset = s && s->modemPreset < PRESET_COUNT ? kPresets[s->modemPreset].channelName : "?";
    snprintf(buf, sizeof buf, "Region %s  Preset %s", s ? s->region : "?", preset);
    setText(s_p1[1], buf);
    snprintf(buf, sizeof buf, "%s%s", strncmp(st.ip, "AP ", 3) == 0 ? "" : "IP ", st.ip);
    setText(s_p1[2], buf);
    snprintf(buf, sizeof buf, "MQTT %s  rx %u  decrypted %u", st.mqtt ? st.mqtt : "?", (unsigned)st.mqttRx,
             (unsigned)st.mqttDecrypted);
    setText(s_p1[3], buf);
    if (st.haveLastRx) {
        formatUptime(st.lastRxAgeSec, t1, sizeof t1);
        snprintf(buf, sizeof buf, "Last RX %s ago  %.0f dBm  %.1f dB", t1, st.lastRssi, st.lastSnr);
    } else {
        snprintf(buf, sizeof buf, "Last RX none yet");
    }
    setText(s_p1[4], buf);
    if (st.dutyLimitPct >= 100) snprintf(buf, sizeof buf, "Airtime (1 h) %.1f%% / none", st.airtimePct);
    else snprintf(buf, sizeof buf, "Airtime (1 h) %.1f%% / %u%%", st.airtimePct, (unsigned)st.dutyLimitPct);
    setText(s_p1[5], buf);
    for (int i = 0; i < kChanRows * 2; i++) {
        if (i < st.chanCount) {
            const char *n = chanOf(st, i);
            snprintf(buf, sizeof buf, "#%s  %d/%d", n && n[0] ? n : "?", st.chanCounts[i], CS_MSGS_PER_CHANNEL);
        } else {
            snprintf(buf, sizeof buf, "%s", i == 0 ? "No channels configured" : "");
        }
        setText(s_p1Chan[i], buf);
    }
    snprintf(buf, sizeof buf, "Flash %u / %u KB   PSRAM free %u KB", (unsigned)st.flashUsedKB,
             (unsigned)st.flashTotalKB, (unsigned)st.psramFreeKB);
    setText(s_p1[6], buf);
    formatUptime(st.uptimeSec, t1, sizeof t1);
    char batt[24];
    if (st.battState == BATT_PRESENT) snprintf(batt, sizeof batt, "%.2f V %u%%", st.battVolts, (unsigned)st.battPct);
    else snprintf(batt, sizeof batt, "USB");
    snprintf(buf, sizeof buf, "Up %s   %s   %s", t1, st.clockSet ? "clock set" : "clock not set", batt);
    setText(s_p1[7], buf);
}

void fillFeed(const DisplayStatus &st) {
    const int n = st.newestMessages ? st.newestMessages(s_msgs, s_msgSlots, kFeedRows) : 0;
    const uint32_t nowUnix = st.clockSet ? (uint32_t)time(nullptr) : 0;
    char head[96], name[32], age[8], text[CS_MAX_TEXT + 8];
    for (int i = 0; i < kFeedRows; i++) {
        setVisible(s_msgHead[i], i < n);
        setVisible(s_msgText[i], i < n);
        if (i >= n) continue;
        const StoredMsg &m = s_msgs[i];
        const char *chan = chanOf(st, s_msgSlots[i]);
        nameOf(st, m.from, name, sizeof name);
        formatAge(messageAgeSec(m, nowUnix, st.clockSet, st.uptimeSec), age, sizeof age);
        snprintf(head, sizeof head, "#%s  %s  %s%s", chan && chan[0] ? chan : "?", name, age,
                 m.source == SRC_MQTT ? "  MQTT" : "");
        setText(s_msgHead[i], head);
        // Cap above the 200-byte text so oneLine only flattens line breaks; the
        // label cuts to its width with "..." (Montserrat here has no U+2026).
        oneLine(m.text, text, sizeof text);
        setText(s_msgText[i], text);
    }
    setVisible(s_msgEmpty, n == 0);
}

void fillActivity(const DisplayStatus &st) {
    const int n = st.newestActivity ? st.newestActivity(s_acts, kActRows) : 0;
    char line[128], name[32], age[8], detail[80];
    for (int i = 0; i < kActRows; i++) {
        setVisible(s_act[i], i < n);
        if (i >= n) continue;
        const ActivityEntry &e = s_acts[i];
        formatAge(st.uptimeSec >= e.uptimeSec ? st.uptimeSec - e.uptimeSec : 0, age, sizeof age);
        if (e.kind == ACT_HELD) snprintf(name, sizeof name, "-");
        else nameOf(st, e.node, name, sizeof name);
        activityDetail(e, chanOf(st, e.chanSlot), detail, sizeof detail);
        snprintf(line, sizeof line, "%s  %s  %s", age, name, detail);
        setText(s_act[i], line);
    }
    setVisible(s_actEmpty, n == 0);
}

}  // namespace

void displayBegin() {
    char err[64] = "";
    if (!dhalBegin(err, sizeof err)) return fail(err);

    const size_t bufBytes = (size_t)kW * kBufLines * 2;   // RGB565
    s_buf1 = (uint8_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_buf2 = (uint8_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_buf1 || !s_buf2) {
        heap_caps_free(s_buf1);
        heap_caps_free(s_buf2);
        s_buf1 = s_buf2 = nullptr;
        return fail("no PSRAM for draw buffers");
    }
    // lv_init() hands LV_MEM_POOL_ALLOC's result to TLSF unchecked: check first.
    if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < LV_MEM_SIZE) return fail("no PSRAM for LVGL heap");

    lv_init();
    lv_tick_set_cb(tickMs);
    s_disp = lv_display_create(kW, kH);
    if (!s_disp) return fail("LVGL display allocation failed");
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_disp, flushCb);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2, (uint32_t)bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *touch = lv_indev_create();
    if (touch) {
        lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(touch, touchReadCb);
        lv_indev_set_display(touch, s_disp);
    } else {
        Serial.printf("[cs] display: no LVGL input device; touch off\n");
    }

    buildMain();
    buildSplash();
    lv_screen_load(s_splash);
    lv_refr_now(s_disp);
    s_splashUntilMs = millis() + kSplashMs;
}

void displayUpdate(const DisplayStatus &st) {
    if (s_off) return;
    const uint32_t now = millis();
    if (!s_started) {
        if ((int32_t)(now - s_splashUntilMs) < 0) {
            lv_timer_handler();
            return;
        }
        endSplash();
        const Settings *s = st.settings;
        const CyclerConfig cfg = s ? CyclerConfig{s->displayPageSec, s->displayDimAfterSec, s->displayBrightness,
                                                  s->displayDimLevel}
                                   : CyclerConfig{30, 120, TFT_BRIGHTNESS_DEFAULT, 0};
        s_cycler.begin(cfg, now);
        s_started = true;
    }

    // tick() before lv_timer_handler(): tap() (from CLICKED, delivered inside
    // the handler) decides wake-vs-advance from the dim state tick() refreshes.
    s_cycler.tick(now, st.battState == BATT_EXTERNAL || st.battState == BATT_ABSENT);
    if (dhalWakePressed()) s_cycler.tap(now);   // a Wake button press counts as a tap
    const uint8_t level = s_cycler.backlight();
    if (level != s_blLevel) {
        dhalBacklight(level);
        s_blLevel = level;
    }

    const int page = s_cycler.page();
    const bool pageChanged = page != s_shownPage;
    if (pageChanged) {
        for (int i = 0; i < 3; i++) {
            setVisible(s_page[i], i == page);
            lv_obj_set_style_bg_color(s_dots[i], i == page ? c565(kAccent) : lv_color_hex(kTextDim), 0);
        }
        s_shownPage = page;
    }
    if (pageChanged || now - s_lastFillMs >= 1000) {
        s_lastFillMs = now;
        fillBar(st);
        if (page == 0) fillHealth(st);
        else if (page == 1) fillFeed(st);
        else fillActivity(st);
    }
    lv_timer_handler();
}
