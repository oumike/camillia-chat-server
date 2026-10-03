# Heltec V4 Expansion Display — Design

**Date:** 2026-10-03
**Status:** Draft for review
**Repo affected:** `camillia-chat-server` only. `camillia-mt` is used as a read-only reference.

## 1. Purpose

The chat server runs on a plain Heltec WiFi LoRa 32 V4 and shows four lines on its 128×64 OLED. This adds a build for the **Heltec V4 with the TFT expansion** (ST7789, 320×240, CHSC6X touch). On that build the server behaves exactly as it does on the plain board. The only difference is that the larger screen shows richer information.

**Success looks like:** a V4 with the expansion boots to a colour splash and then cycles through three pages: health, recent messages and sync activity. The pages change every 30 s or when the screen is tapped. Radio, store, protocol, web config and MQTT are unchanged. The plain V4 build and its OLED screen are unchanged too.

## 2. Scope

**In scope**
- A new PlatformIO env, `heltec-v4-expansion`, with its own board header and an LVGL display backend.
- Three rotating pages, touch to advance, and a backlight rule that depends on the power source.
- A node-name table learned from NODEINFO, used for sender names.
- An in-RAM activity log of sync events.
- Last-RX RSSI/SNR and last-hour airtime, exposed for page 1.
- Four display settings (NVS, YAML, web config).
- CI build and release of the new env, and a README hardware note.

**Out of scope**
- Using the expansion's GPS or environment sensors.
- Saving the name table to flash (it is RAM only and refills after boot).
- Any change to what the OLED shows.
- Showing names in the web UI. The table makes this easy later, but it is not part of this work.
- Swipes, buttons or any other interaction beyond a tap.

## 3. Reference: camillia-mt

camillia-mt already ships a build for this exact hardware (`env:heltec-v4`, `DEVICE_HELTEC_V4_EXPANSION`). It is the reference for everything below that touches the board:

- `src/hal/hw_heltec_v4.h`: pins, power-up order, and the reasons behind them.
- `src/hal/display.h`: `Panel_HeltecV4Tft` (a `Panel_ST7789` subclass that adds a gamma-curve init command; the stock init looks washed out) and `Touch_Heltec_CHSC6X` (an `lgfx::ITouch` wrapper that handles firmwares with swapped axes).
- `src/lv_conf.h` and `src/main_lvgl.cpp`: LVGL 9.5.0 on LovyanGFX, flush and pointer glue.
- `drawBootSplash`: the colour camellia.

The panel and touch classes are copied in with a one-line provenance comment, in the same way `camillia_mesh` is vendored. The "do not revisit" notes on GPIO36 are copied word for word.

### 3.1 Hardware facts carried over

| Item | Value | Note |
|---|---|---|
| VEXT rail | GPIO36, **LOW** = on, driven at the very top of `setup()` and never changed | HIGH breaks touch I2C; raising it late stops the board booting |
| Module power hold | GPIO7 HIGH | already driven as `LORA_FEM_POWER_PIN` |
| TFT SPI | SPI3, SCK 17, MOSI 33, MISO −1 (3-wire), CS 15, DC 16, RST 18, 40 MHz write | |
| Backlight | GPIO21 PWM, 44.1 kHz, channel 7, not inverted, default 160 | |
| Panel | 240×320 native, driven landscape 320×240, invert on, BGR off | |
| Touch | CHSC6X on `Wire1`, SDA 47, SCL 48, addr 0x2E, RST 44, no INT (polled) | |
| LoRa | unchanged from the plain V4 | |

The expansion takes GPIO17, 18 and 21, which are the OLED's SDA, SCL and RST. That is why this has to be a separate build and not a runtime option.

## 4. Build structure

### 4.1 Envs

- `heltec-v4` (unchanged behaviour) builds `status_display_oled.cpp`.
- `heltec-v4-expansion` extends the common settings, adds `-DCS_BOARD_V4_EXPANSION=1`, `-DLV_CONF_INCLUDE_SIMPLE`, and these lib_deps:
  - `lovyan03/LovyanGFX@^1.1.16`
  - `lvgl/lvgl@9.5.0`
  - `https://github.com/Quency-D/chsc6x/archive/3b2b6cebf3177b3e2c33d06e07909b0b10159516.zip`

  It builds `status_display_lvgl.cpp` and drops the SSD1306 library.
- `heltec-v4-test` is unchanged. There is no expansion test variant; `CS_SERIAL_TEST` can be added to it by hand if ever needed.
- `native` gains the new pure units.

`build_src_filter` makes sure exactly one `status_display_*.cpp` is compiled per env.

### 4.2 Board headers

`src/board.h` becomes a selector:

```cpp
#if defined(CS_BOARD_V4_EXPANSION)
#include "board_v4_exp.h"
#else
#include "board_v4.h"
#endif
```

`board_v4.h` is today's `board.h`. `board_v4_exp.h` has the same LoRa and battery pins, plus the TFT, backlight and touch pins from §3.1, and no OLED pins.

## 5. Units

| Unit | Purpose | Arduino? | Tests |
|---|---|---|---|
| `node_names.{h,cpp}` | Node ID → long and short name | no | native |
| `activity_log.{h,cpp}` | Ring of recent sync events | no | native |
| `page_cycler.{h,cpp}` | Page rotation, tap handling and backlight level | no | native |
| `status_display.h` | Shared API (extended) | – | – |
| `status_display_oled.cpp` | Today's `status_display.cpp`, renamed, behaviour unchanged | yes | device |
| `status_display_lvgl.cpp` | Panel, touch, LVGL, splash and the three pages | yes | device |
| `lv_conf.h` | Trimmed LVGL config, expansion env only | – | build |

`node_names` and `activity_log` are compiled into both device builds. They are cheap, and they keep `main.cpp` free of `#ifdef`s.

### 5.1 `node_names`

- Capacity **200** entries in PSRAM, about 8 KB: `{uint32_t id; char longName[25]; char shortName[5]; uint32_t lastHeardMs;}`.
- `void update(uint32_t id, const char *longName, const char *shortName, uint32_t nowMs)`: adds or replaces. An empty long name in an update leaves the stored long name alone, and the same goes for the short name. When the table is full, the least recently heard entry is replaced.
- `void displayName(uint32_t id, char *out, size_t cap) const`: the long name if known and non-empty, else the short name, else `!xxxxxxxx` (8 lowercase hex digits).
- Filled from `NODEINFO_APP` packets heard on LoRa and on MQTT, via the vendored `decodeUser()`. Our own node is never added.

### 5.2 `activity_log`

- Ring of **50** entries: `{uint32_t uptimeSec; uint32_t unix; uint8_t kind; uint32_t node; int8_t chanSlot; uint32_t a; uint32_t b; uint8_t flags;}`.
- Kinds and what they show on page 3:

| Kind | Logged when | Detail shown |
|---|---|---|
| `ACT_DISCOVER` | a DISCOVER is received | `DISCOVER` |
| `ACT_ANNOUNCE` | an ANNOUNCE is sent | `ANNOUNCE sent` |
| `ACT_REQUEST` | a REQUEST is received | `REQUEST #chan from seq N` (or `from start`) |
| `ACT_BATCH` | the last packet of a transfer is sent | `sent P packets (M msgs)` plus `, MORE` |
| `ACT_HELD` | `serveLoop` is held by the airtime budget, logged at most once per minute | `held: airtime limit` |
| `ACT_TX_FAIL` | `radioSend` fails | `send FAILED` |

- `int copyNewest(Entry *out, int max) const`: newest first.
- A transfer's packets are added up in `main.cpp` (per `to` and slot) and logged as one `ACT_BATCH` entry when the packet with `FLAG_LAST` goes out. Individual packets still go to serial as they do now.

### 5.3 `page_cycler`

The pure state machine behind §6.3 and §6.4.

```cpp
struct CyclerConfig { uint16_t pageSec; uint16_t dimAfterSec; uint8_t brightness; uint8_t dimLevel; };
class PageCycler {
public:
    void    begin(const CyclerConfig &cfg, uint32_t nowMs);
    void    tick(uint32_t nowMs, bool onExternalPower);   // advances on timeout, updates the dim state
    void    tap(uint32_t nowMs);                         // wake if dimmed, else advance
    int     page() const;                                // 0, 1, 2
    uint8_t backlight() const;                           // level to write to the PWM
};
```

- The page goes 0 → 1 → 2 → 0 every `pageSec`, measured from the last change (the timer or a tap).
- `tap` while dimmed: wakes the screen, keeps the page and restarts the page timer. `tap` while bright: moves to the next page and restarts the page timer.
- `onExternalPower` true (USB, or no cell connected): the level is always `brightness`. False: `brightness` until `dimAfterSec` after the last tap, the boot, or the moment external power was lost; after that, `dimLevel`. `dimAfterSec == 0` means it never dims.
- Pages keep rotating while dimmed.

## 6. Display (expansion build)

### 6.1 LVGL setup

- LVGL 9.5.0 with `LV_COLOR_DEPTH 16`. The LVGL heap is in PSRAM (`LV_MEM_SIZE` 128 KB). Only Montserrat 12, 14, 16 and 20 are enabled. Logging is off.
- Two partial draw buffers of 320×40 pixels in PSRAM. The flush callback pushes through LovyanGFX (`pushImageDMA` or the same call camillia-mt uses).
- The LVGL tick comes from `millis()` (`lv_tick_set_cb`).
- One pointer input device reads `Touch_Heltec_CHSC6X`. A `LV_EVENT_CLICKED` on the screen calls `PageCycler::tap`.
- All objects are created once in `displayBegin()`. The lists on pages 2 and 3 are fixed rows of labels whose text is replaced. Nothing is created or deleted at run time, so the LVGL heap stays flat.

### 6.2 Layout (landscape 320×240)

**Status bar (every page, top 20 px):** "Camillia Chat Server" on the left. On the right: the clock `HH:MM` (only when the clock is set), the battery icon (same states as the OLED: absent, external, or percent) and three page dots.

**Page 1: Health**

| Section | Content |
|---|---|
| Node | long name and `!nodeid`, short name, region and preset |
| Network | IP or `AP x.x.x.x`, MQTT state and received/decrypted counts |
| Radio | last RX `12s ago  -97 dBm  6.5 dB` (or `none yet`), airtime in the last hour against the limit, e.g. `0.8% / 10%` (`/ none` when the limit is 100%) |
| Store | per channel `name  count/250`, up to 10 rows in two columns; flash used/total KB; free PSRAM KB |
| System | uptime `3d 4h`, `clock set` or `clock not set`, battery `3.92 V 78%` (or `USB`) |

**Page 2: Message feed.** The newest 7 stored messages across all channels, newest first, ordered by receive time (unix when set, otherwise uptime this boot; messages loaded from a previous boot come after messages heard this boot). Each message takes two lines:
- line 1: `#chan`, the sender's `displayName`, the age (`now`, `3m`, `2h`, `4d`, or `?` when unknown), and an `MQTT` badge if the source is MQTT
- line 2: the text on one line, cut with `…`

Empty state: "No messages yet".

**Page 3: Sync activity.** The newest 11 `activity_log` entries, one line each: age, the node's `displayName` (`-` for `ACT_HELD`), and the detail from §5.2. Empty state: "No sync activity yet".

**Redraw:** label text is updated at most once a second (as on the OLED), and immediately when the page changes. `lv_timer_handler()` runs on every `displayUpdate()` call.

### 6.3 Touch

A tap anywhere calls `PageCycler::tap` (§5.3). There are no other gestures.

### 6.4 Backlight

LEDC PWM on GPIO21. Every `displayUpdate()`, `PageCycler::backlight()` is written whenever it changes. "External power" is `battState == BATT_EXTERNAL || battState == BATT_ABSENT`.

### 6.5 Boot splash

The colour camellia from camillia-mt's `drawBootSplash` (same geometry and palette), "Camillia Chat Server", and `v<version>`. It is shown for 3 s from `displayBegin()`, the same as the OLED splash, and then page 1 appears.

### 6.6 Power-up order

The first line of `setup()` in the expansion build drives GPIO36 LOW. The rest of `setup()` stays as it is today, with `displayBegin()` in its current position. `displayBegin()` must not touch GPIO36 again.

## 7. Shared API and `main.cpp` hooks

`DisplayStatus` gains:

```cpp
const Settings *settings;          // names, region, preset, display settings
uint32_t nodeId;
uint32_t uptimeSec;
bool     clockSet;
float    battVolts;
uint32_t mqttRx, mqttDecrypted;
bool     haveLastRx; uint32_t lastRxAgeSec; float lastRssi, lastSnr;
float    airtimePct; uint8_t dutyLimitPct;
int      chanCount; int chanCounts[CS_MAX_CHANNELS];
uint32_t flashUsedKB, flashTotalKB, psramFreeKB;
// Newest-first copies for pages 2 and 3; filled only when the page is shown.
int (*newestMessages)(StoredMsg *out, int8_t *slots, int max);
int (*newestActivity)(ActivityEntry *out, int max);
const NodeNames *names;
```

The OLED backend ignores the new fields.

In `main.cpp`:
- `radioPoll` path: record `rssi`, `snr` and `millis()` for every decrypted packet. Route `NODEINFO_APP` to `node_names` (from LoRa and from the MQTT ingest callback).
- `handleChatServerPacket`: log `ACT_DISCOVER` / `ACT_REQUEST`.
- `serveLoop`: log `ACT_HELD` (at most once a minute), `ACT_ANNOUNCE`, `ACT_TX_FAIL`, and the summed `ACT_BATCH` on `FLAG_LAST`.
- `AirtimeBudget` gains `uint32_t usedMs(uint32_t nowMs)` so the last-hour percentage can be shown.
- The `displayUpdate` call fills the new fields. Flash usage is read at most every 10 s.

## 8. Settings

New fields in `Settings`, with defaults and range checks in `settingsValidate`, YAML keys, NVS and a "Display" block in the web config:

| Field | YAML key | Default | Range |
|---|---|---|---|
| `displayBrightness` | `display.brightness` | 160 | 10–255 |
| `displayDimAfterSec` | `display.dim_after_sec` | 120 | 0 (never) – 3600 |
| `displayDimLevel` | `display.dim_level` | 0 | 0–255, must be below brightness |
| `displayPageSec` | `display.page_sec` | 30 | 10–300 |

Both builds store and validate these, so a YAML export moves between boards unchanged. The web block notes "Used by the TFT expansion build only". If an NVS blob from before this change is shorter, the defaults are used, following whatever versioning `settings_nvs.cpp` already does.

## 9. Error handling

| Failure | Behaviour |
|---|---|
| Panel init, LVGL heap or draw buffers fail | Log `[cs] display: <reason>`, mark the display off, and make `displayUpdate()` return straight away. Everything else runs. |
| Touch controller doesn't answer at init, or fails 3 reads in a row | Log once and stop polling it, so there are no repeated 1 s I2C timeouts. Pages still rotate. With no taps, the backlight follows the power rule. |
| Name table full | Replace the least recently heard entry. |
| Activity ring full | Overwrite the oldest entry. |
| Display setting out of range (YAML or web) | Reject it with a reason, as other settings are rejected today. |

## 10. Testing

**Native (`pio test -e native`)**
- `node_names`: the long → short → `!id` fallback; partial updates keeping the other name; eviction of the least recently heard entry when full. (Skipping our own ID is the caller's job in `main.cpp` and is checked on hardware.)
- `activity_log`: wrap-around at 50; newest-first copy; copy with `max` smaller than the count.
- `page_cycler`: rotation 0→1→2→0 at `pageSec`; a tap advancing and restarting the timer; a tap while dimmed only waking; on battery dimming after `dimAfterSec`; external power never dimming; losing external power starting the timeout; `dimAfterSec == 0` never dimming.
- `settings` / `settings_yaml`: defaults, range rejection, and a YAML round-trip of the four new keys.

**Build:** `heltec-v4`, `heltec-v4-test` and `heltec-v4-expansion` all build. The plain build's OLED code is byte-for-byte the renamed file.

**On hardware (V4 with the expansion, flashed only when the owner asks):**
1. Splash for about 3 s, then page 1 with correct values.
2. Pages rotate every 30 s; a tap advances and restarts the timer.
3. After a NodeInfo is heard from a node, its long name shows on page 2.
4. A camillia-mt "Check for Messages Now" shows `REQUEST` and `sent … packets` on page 3.
5. On battery, the backlight goes off after 120 s; a tap wakes it without changing the page; plugging in USB restores full brightness.
6. The plain V4 build still shows the unchanged OLED page.

## 11. Release

- `build.yml`: add `pio run -e heltec-v4-expansion`.
- `release.yml`: build and attach the `heltec-v4-expansion` binaries next to `heltec-v4`, with the same naming scheme.
- README: a hardware note naming both boards and the env to flash for each.
