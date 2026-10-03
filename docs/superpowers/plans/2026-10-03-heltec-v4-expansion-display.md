# Heltec V4 Expansion Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `heltec-v4-expansion` build of the chat server that behaves exactly like `heltec-v4`, and shows health, a message feed and sync activity on the 320×240 TFT. The pages rotate every 30 s or on a tap.

**Architecture:** The pure units (`node_names`, `activity_log`, `page_cycler`, `display_text`, feed merge) are host-tested in `native`. `main.cpp` feeds them on both boards. The two backends sit behind `status_display.h`, and one is compiled per env: the OLED backend (today's code, renamed) and an LVGL 9.5.0 backend on LovyanGFX with camillia-mt's panel and touch classes.

**Tech Stack:** PlatformIO, Arduino-ESP32 (espressif32@7.0.1), Unity for `native` tests, LVGL 9.5.0, LovyanGFX ^1.1.16, chsc6x (pinned zip).

**Spec:** `docs/superpowers/specs/2026-10-03-heltec-v4-expansion-display-design.md`

**Reference (read-only):** `../camillia-mt/src/hal/hw_heltec_v4.h`, `../camillia-mt/src/hal/display.h`, `../camillia-mt/src/lv_conf.h`, `../camillia-mt/src/main_lvgl.cpp` (search `drawBootSplash`, `lv_display_set_flush_cb`, `lv_indev_create`).

## Global Constraints

- No `pio run` / builds or flashing unless the owner has asked in this session. Native tests (`pio test -e native`) are fine. Build steps below are run only when the owner asks; otherwise mark them "not run" in the report.
- The plain `heltec-v4` behaviour and OLED screen must be unchanged.
- VEXT GPIO36 is driven **LOW** at the very top of `setup()` on the expansion build and never touched again. Copy camillia-mt's GPIO36 comments word for word.
- LVGL **9.5.0** exactly, LovyanGFX `^1.1.16`, chsc6x `https://github.com/Quency-D/chsc6x/archive/3b2b6cebf3177b3e2c33d06e07909b0b10159516.zip`.
- Display defaults: brightness **160**, dim after **120 s**, dim level **0**, page every **30 s**. Ranges: brightness 10–255, dimAfterSec 0–3600 (0 = never), dimLevel 0–255 and below brightness, pageSec 10–300.
- Capacities: name table **200**, activity ring **50**, feed **7** messages, activity page **11** rows.
- Vendored files get a first line `// Vendored from camillia-mt <commit>: <path>; edit there, not here.`
- Commit messages: conventional (`feat:`, `fix:`, `build:`, `docs:`), ending with the session's Co-Authored-By / Claude-Session lines.

**Deviation from the spec (decided here):** the YAML keys follow the file's existing camelCase-under-a-section style, so it's a `display:` section with `brightness`, `dimAfterSec`, `dimLevel` and `pageSec`, not `display.dim_after_sec`. Task 1 updates the spec's §8 table to match.

## Review Focus

1. **Upgrading a running server:** `settings_nvs.cpp` throws away any blob whose version or size differs, so adding fields would silently reset channels, keys and WiFi to defaults. A v4 blob has to load with display defaults filled in. *Test: Task 1 `test_upgrade_v4_blob`.*
2. **Long or multibyte names:** `UserInfo.longName` is 40 bytes, but the table keeps 24 bytes plus NUL. A name cut in the middle of a UTF-8 character gives LVGL garbage. *Test: Task 2 `test_long_name_cut_on_utf8_boundary`.*
3. **Message text with newlines:** a text containing `\n` would break the one-line feed row. *Test: Task 4 `test_one_line_replaces_newlines`.*
4. **Ages when the clock isn't set or the message came from a previous boot:** `messageAgeSec` returns `csp::AGE_UNKNOWN`, which must show `?`, not a huge number. *Test: Task 4 `test_format_age`.*
5. **`flash.sh` with two boards' images in one folder:** it picks "the newest `camillia-chat-server-*.bin`", which could be the wrong board's image. With more than one board present and no file argument, it must refuse and list them. *Test: Task 7, Step 3.*

---

### Task 1: Display settings (fields, validation, YAML, NVS upgrade, web)

**Files:**
- Modify: `src/settings.h`, `src/settings.cpp`, `src/settings_yaml.cpp`, `src/settings_nvs.cpp`, `src/web_ui.cpp`
- Test: `test/test_settings/test_main.cpp`, `test/test_settings_yaml/test_main.cpp`
- Modify: spec §8 (YAML key names)

**Interfaces:**
- Produces: these `Settings` fields, **appended after `tz`**: `uint8_t displayBrightness; uint16_t displayDimAfterSec; uint8_t displayDimLevel; uint16_t displayPageSec;`
- Produces: `bool settingsFromBlob(const uint8_t *blob, size_t len, uint8_t ver, Settings &out);` in `settings.cpp` (pure)

- [ ] **Step 1: Write the failing tests** in `test_settings`:
  - `test_display_defaults`: after `settingsDefaults`, the four fields are 160, 120, 0, 30, and the settings validate.
  - `test_display_ranges`: each of these fails validation with a message naming the field: brightness 9, dimAfterSec 3601, dimLevel 160 with brightness 160, pageSec 9, pageSec 301. Brightness 255 / dimLevel 254 / dimAfterSec 0 / pageSec 10 pass.
  - `test_upgrade_v4_blob`: start from defaults with `longName = "Old"` and `chanCount = 2`. Take the first `offsetof(Settings, displayBrightness)` bytes as the blob. `settingsFromBlob(blob, thatLen, 4, s)` returns true, with `longName == "Old"`, `chanCount == 2`, brightness 160 and pageSec 30.
  - `test_blob_current_and_bad`: a full-size blob at version 5 round-trips. A wrong length at version 5, a wrong length at version 4, and version 3 all return false.

  In `test_settings_yaml`: `test_display_roundtrip` exports non-default values (200/600/20/45), imports them back and compares. `test_display_bad_value` checks that `display:\n  pageSec: 5\n` is rejected with an error containing `pageSec`.

- [ ] **Step 2: Run** `pio test -e native -f test_settings -f test_settings_yaml`. Expected: FAIL (fields not defined).

- [ ] **Step 3: Implement.**
  - Defaults and validation messages in the existing style: `"Screen brightness must be 10-255"`, `"Dim after must be 0-3600 s"`, `"Dim level must be below brightness"`, `"Page time must be 10-300 s"`.
  - `settingsFromBlob`: version 5 needs `len == sizeof(Settings)` and is a straight copy. Version 4 needs `len == offsetof(Settings, displayBrightness)`; it loads defaults first, then copies the blob over the front. Anything else returns false.
  - `settings_nvs.cpp`: set `kSettingsVersion = 5`. `settingsLoad` reads the blob into a `sizeof(Settings)` buffer and calls `settingsFromBlob`.
  - YAML: write a `display:` section after `wifi:`, with keys `brightness`, `dimAfterSec`, `dimLevel` and `pageSec`, parsed with `toLong` like `replies`.
  - Web: a `<fieldset><legend>Display</legend>` before WiFi with four `numInput`s (`dispBright`, `dispDim`, `dispDimLvl`, `dispPage`), plus the help line "Used by the TFT expansion build only." Parse them with `constrain` like `batch`.
  - Spec §8: change the YAML key column to `display.brightness`, `display.dimAfterSec`, `display.dimLevel`, `display.pageSec`.

- [ ] **Step 4: Run** `pio test -e native`. Expected: all pass.

- [ ] **Step 5: Commit** `feat: display settings (brightness, dim, page time) with v4 NVS upgrade`.

---

### Task 2: `node_names`

**Files:**
- Create: `src/node_names.h`, `src/node_names.cpp`
- Test: `test/test_node_names/test_main.cpp`
- Modify: `platformio.ini` (`[env:native]` `build_src_filter`: add `+<node_names.cpp>`)

**Interfaces:**
- Produces:
  ```cpp
  constexpr int NODE_NAMES_CAP = 200;
  class NodeNames {
  public:
      using AllocFn = void *(*)(size_t);
      bool begin(AllocFn alloc);                 // allocates NODE_NAMES_CAP entries once
      void update(uint32_t id, const char *longName, const char *shortName, uint32_t nowMs);
      void displayName(uint32_t id, char *out, size_t cap) const;
      int  count() const;
  };
  ```

- [ ] **Step 1: Write the failing tests:**
  - `test_fallback_order`: an unknown ID gives `"!0000abcd"`. After `update(id, "", "AB", t)` it gives `"AB"`. After `update(id, "Alice", "", t)` it gives `"Alice"`, and the short name is still kept (a later update with an empty long name doesn't wipe "Alice").
  - `test_evicts_least_recent`: fill 200 IDs at times 1..200. Update ID #1 at time 300. Add a new ID: ID #2 is gone (falls back to `!`), and ID #1 is kept.
  - `test_long_name_cut_on_utf8_boundary`: a 30-byte long name made of `"é"` (2 bytes each) is stored as 24 bytes, ending on a whole character.
  - `test_null_names_ok`: `update(id, nullptr, nullptr, t)` doesn't crash and leaves the ID unknown.
- [ ] **Step 2: Run** `pio test -e native -f test_node_names`. Expected: FAIL (missing header).
- [ ] **Step 3: Implement** a linear search over the array, which is fine at 200. Store at most 24 bytes for the long name and 4 for the short name, cut at a UTF-8 lead byte.
- [ ] **Step 4: Run** `pio test -e native`. Expected: all pass.
- [ ] **Step 5: Commit** `feat: node name table learned from NODEINFO`.

---

### Task 3: `activity_log`

**Files:**
- Create: `src/activity_log.h`, `src/activity_log.cpp`
- Test: `test/test_activity_log/test_main.cpp`
- Modify: `platformio.ini` native filter (`+<activity_log.cpp>`)

**Interfaces:**
- Produces:
  ```cpp
  enum ActivityKind : uint8_t { ACT_DISCOVER, ACT_ANNOUNCE, ACT_REQUEST, ACT_BATCH, ACT_HELD, ACT_TX_FAIL };
  struct ActivityEntry {
      uint32_t uptimeSec, unix;   // unix 0 = clock unset
      uint8_t  kind;
      uint32_t node;              // 0 for ACT_HELD
      int8_t   chanSlot;          // -1 discovery / none
      uint32_t a, b;              // REQUEST: a=cursor; BATCH: a=packets, b=messages
      uint8_t  flags;             // BATCH: 1 = MORE
  };
  constexpr int ACTIVITY_CAP = 50;
  class ActivityLog {
  public:
      void add(const ActivityEntry &e);
      int  copyNewest(ActivityEntry *out, int max) const;
      int  count() const;
  };
  // Detail text per spec §5.2, e.g. "REQUEST #LongFast from seq 41", "sent 3 packets (24 msgs), MORE".
  void activityDetail(const ActivityEntry &e, const char *chanName, char *out, size_t cap);
  ```
- [ ] **Step 1: Write the failing tests:** `test_wraps_at_cap` (add 60, count 50, newest first gives a=59..10); `test_copy_max_smaller`; `test_detail_text` with one case per kind. Expected strings: `"DISCOVER"`, `"ANNOUNCE sent"`, `"REQUEST #LongFast from seq 41"`, `"REQUEST #LongFast from start"` (a=0), `"sent 3 packets (24 msgs), MORE"`, `"sent 1 packet (1 msg)"`, `"held: airtime limit"`, `"send FAILED"`.
- [ ] **Step 2: Run** `pio test -e native -f test_activity_log`. Expected: FAIL.
- [ ] **Step 3: Implement** a static array ring (50 × ~28 B, held as a static in main).
- [ ] **Step 4: Run** `pio test -e native`. Expected: all pass.
- [ ] **Step 5: Commit** `feat: in-RAM sync activity log`.

---

### Task 4: `page_cycler`, `display_text` and the feed merge

**Files:**
- Create: `src/page_cycler.{h,cpp}`, `src/display_text.{h,cpp}`
- Modify: `src/channel_store.{h,cpp}` (add the merge helper)
- Test: `test/test_page_cycler/test_main.cpp`, `test/test_display_text/test_main.cpp`, `test/test_channel_store/test_main.cpp`
- Modify: `platformio.ini` native filter (`+<page_cycler.cpp> +<display_text.cpp>`)

**Interfaces:**
- Produces `PageCycler` / `CyclerConfig` exactly as in spec §5.3.
- Produces in `display_text.h`:
  ```cpp
  void formatAge(uint32_t sec, char *out, size_t cap);      // "now" <60s, "3m", "2h", "4d", "?" for csp::AGE_UNKNOWN
  void formatUptime(uint32_t sec, char *out, size_t cap);   // "45s", "12m", "5h 3m", "3d 4h"
  void oneLine(const char *in, char *out, size_t cap);      // \r \n \t -> space; cut with "…" (UTF-8) to fit cap
  ```
- Produces in `channel_store.h`:
  ```cpp
  // Newest messages across stores, newest first. A message heard this boot
  // (rxUptimeSec > 0) is newer than one from a previous boot; two this-boot
  // messages compare by rxUptimeSec; two previous-boot ones by rxUnix, then seq.
  int newestAcross(const ChannelStore *stores, int count, StoredMsg *out, int8_t *slots, int max);
  ```
- [ ] **Step 1: Write the failing tests.**
  - `test_page_cycler`:
    - `test_rotates`: config {30,120,160,0}, external power. Pages at 0 / 29.9 s / 30 s / 60 s / 90 s are 0, 0, 1, 2, 0.
    - `test_tap_advances_and_restarts`: a tap at 10 s gives page 1. Still 1 at 39 s, 2 at 40 s.
    - `test_battery_dims`: on battery, backlight is 160 at 119 s and 0 at 120 s.
    - `test_tap_while_dim_only_wakes`: dim at 130 s, then a tap gives backlight 160 and the page unchanged. The next page change is 30 s after the tap.
    - `test_external_never_dims`: external power at 10 000 s gives 160.
    - `test_power_loss_starts_timeout`: external until 500 s, battery from 500 s. Backlight 160 at 619 s and 0 at 620 s.
    - `test_zero_never_dims`: dimAfterSec 0 on battery at 10 000 s gives 160.
  - `test_display_text`: `test_format_age` (0 → "now", 59 → "now", 60 → "1m", 7200 → "2h", 345600 → "4d", `csp::AGE_UNKNOWN` → "?"); `test_format_uptime` (45 → "45s", 720 → "12m", 18180 → "5h 3m", 273600 → "3d 4h"); `test_one_line_replaces_newlines` ("a\nb\r\nc" → "a b  c"); `test_one_line_cuts_with_ellipsis` (a 40-char input with cap 12 gives a result ending in "…" that is ≤ 11 bytes plus NUL and doesn't cut a multibyte character).
  - `test_channel_store`: `test_newest_across` uses two stores with interleaved this-boot uptimes, plus a previous-boot message (rxUptimeSec 0, rxUnix set). The order comes out newest-first by uptime, the previous-boot one comes last, and `slots[]` match.
- [ ] **Step 2: Run** `pio test -e native`. Expected: the new tests FAIL.
- [ ] **Step 3: Implement.** `newestAcross` is a k-way merge from each store's head, using the comparator in the comment, with `copyAfter` reading one message at a time.
- [ ] **Step 4: Run** `pio test -e native`. Expected: all pass.
- [ ] **Step 5: Commit** `feat: page cycler, display text helpers and cross-channel newest messages`.

---

### Task 5: Shared display API and `main.cpp` hooks (both boards)

**Files:**
- Modify: `src/status_display.h`, `src/main.cpp`, `src/airtime.{h,cpp}`
- Rename: `src/status_display.cpp` → `src/status_display_oled.cpp` (no code change besides includes)
- Test: `test/test_airtime/test_main.cpp`

**Interfaces:**
- Consumes: Tasks 1–4.
- Produces: `uint32_t AirtimeBudget::usedMs(uint32_t nowMs);` (expires old entries first) and `DisplayStatus` as in spec §7, with the callbacks typed:
  ```cpp
  int (*newestMessages)(StoredMsg *out, int8_t *slots, int max);
  int (*newestActivity)(ActivityEntry *out, int max);
  const char *(*chanName)(int slot);
  const NodeNames *names;
  const Settings *settings;
  ```
  Also `int chanCounts[CS_MAX_CHANNELS]`.
- [ ] **Step 1: Write the failing test** `test_used_ms`: record 1000 ms at t=0 and 500 ms at t=1 800 000. `usedMs(1 900 000)` is 1500, and `usedMs(3 600 001)` is 500.
- [ ] **Step 2: Run** `pio test -e native -f test_airtime`. Expected: FAIL.
- [ ] **Step 3: Implement `usedMs`, then the hooks** in `main.cpp`:
  - Static `NodeNames s_names` (begun with `ps_malloc` in `setup()`), `ActivityLog s_activity`, and the last RX (`ms`, `rssi`, `snr`, a have-flag), updated for every packet `radioPoll` returns.
  - In `ingest`, before the text filter: if `pkt.portnum == NODEINFO_APP && pkt.hdr.from != s_nodeId` and `decodeUser` succeeds, call `s_names.update`. This runs for LoRa and MQTT, since both go through `ingest`.
  - `handleChatServerPacket`: add `ACT_DISCOVER` or `ACT_REQUEST` (decode the REQUEST for `a = cursor`).
  - `serveLoop`:
    - `ACT_HELD` when `canSend` is false, no more than once every 60 000 ms.
    - `ACT_TX_FAIL` when `radioSend` fails.
    - `ACT_ANNOUNCE` for an announce.
    - For BATCH packets, keep a running `{to, slot, packets, msgs}` and add `ACT_BATCH` when the packet with `FLAG_LAST` goes out (flags bit 1 = MORE). Decoding the header and items reuses what `logOutgoing` already decodes, so return them from it rather than decoding twice.
  - Fill the new `DisplayStatus` fields. Flash usage is cached and refreshed every 10 s.
  - The OLED backend ignores the new fields.
- [ ] **Step 4: Run** `pio test -e native` → all pass. Build check only if the owner asks: `pio run -e heltec-v4` → SUCCESS.
- [ ] **Step 5: Commit** `feat: name table, activity log and radio stats feeding the display`.

---

### Task 6: `heltec-v4-expansion` env and LVGL backend

**Files:**
- Rename: `src/board.h` → `src/board_v4.h`. Create: `src/board.h` (selector, spec §4.2), `src/board_v4_exp.h`
- Create: `src/lv_conf.h`, `src/hal_v4_exp_display.h` (vendored `Panel_HeltecV4Tft`, `Touch_Heltec_CHSC6X`, and an `LGFX_V4Exp` device class), `src/status_display_lvgl.cpp`
- Modify: `platformio.ini`, `src/main.cpp` (VEXT LOW at the top of `setup()`, under `#if defined(CS_BOARD_V4_EXPANSION)`)

**Interfaces:**
- Consumes: `status_display.h` (Task 5), `PageCycler` and the `display_text` helpers (Task 4), `activityDetail` (Task 3), `NodeNames::displayName` (Task 2).
- Produces: `displayBegin()` / `displayUpdate()` for the expansion env.

- [ ] **Step 1: platformio.ini.**
  - Move the shared lines into `[common]` (`platform`, `board`, flash and partitions, `build_flags`, base `lib_deps` without SSD1306).
  - `heltec-v4` extends it and adds the SSD1306 lib, with `build_src_filter = +<*> -<status_display_lvgl.cpp>`.
  - `heltec-v4-expansion` extends it and adds `-DCS_BOARD_V4_EXPANSION=1 -DLV_CONF_INCLUDE_SIMPLE -Isrc` and the three libs from Global Constraints, with `build_src_filter = +<*> -<status_display_oled.cpp>`.
  - `heltec-v4-test` keeps extending `heltec-v4`.
- [ ] **Step 2: Board header and vendored HAL.**
  - `board_v4_exp.h`: pins from spec §3.1. Copy camillia-mt's GPIO36 comment block word for word.
  - `hal_v4_exp_display.h`: copy the two classes from `camillia-mt/src/hal/display.h` (the `DEVICE_HELTEC_V4_EXPANSION` branches only), plus a device class configuring SPI3 / panel / light (PWM ch 7, 44.1 kHz) / touch on `Wire1` with the §3.1 values. Panel offsets and rotation are copied from camillia-mt's landscape config.
- [ ] **Step 3: `lv_conf.h`.**
  - Start from camillia-mt's file, then trim it to: `LV_COLOR_DEPTH 16`; `LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN` with `LV_MEM_POOL_INCLUDE "esp_heap_caps.h"` and `LV_MEM_POOL_ALLOC(size) heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`; `LV_MEM_SIZE (128U * 1024U)`; `LV_USE_LOG 0`.
  - Only `LV_FONT_MONTSERRAT_12/14/16/20` are on, with default font 14.
  - Only the label, canvas and object widgets (canvas for the splash); everything else disabled where camillia-mt's file has a switch.
- [ ] **Step 4: `status_display_lvgl.cpp`.**
  - `displayBegin()`: init the device, set landscape rotation, backlight 160. Two 320×40 RGB565 buffers via `heap_caps_malloc(SPIRAM)`. `lv_init`, `lv_tick_set_cb(millis-wrapper)`, display create / flush (`startWrite` + `setAddrWindow` + `writePixels` + `endWrite` + `lv_display_flush_ready`, as in camillia-mt), pointer indev reading the touch.
    - If any of these fails: `Serial.printf("[cs] display: %s\n", reason)`, set `s_off = true` and return.
  - Build the objects: splash screen, status bar, and three page containers (page 2: 7 × 2 labels; page 3: 11 labels; page 1: one label per row in spec §6.2). The splash is the colour camellia from camillia-mt's `drawBootSplash` geometry and palette, drawn on an `lv_canvas` (RGB565, in PSRAM, freed after the splash), plus title and version. It shows for 3000 ms.
  - Screen `LV_EVENT_CLICKED` → `s_cycler.tap(millis())`.
  - Touch errors: 3 failed reads in a row → log once, then the indev read reports released from then on.
  - `displayUpdate(st)`:
    - Returns if `s_off`.
    - `PageCycler::begin` on the first call after the splash, using `st.settings` display fields.
    - Every call: `tick(now, external)` where external is `st.battState == BATT_EXTERNAL || st.battState == BATT_ABSENT`; write the backlight when it changes; show the current page container.
    - At most once a second, or on a page change: refill the visible page's labels using `formatAge`, `formatUptime`, `oneLine`, `displayName` and `activityDetail`.
    - Then `lv_timer_handler()`.
- [ ] **Step 5: Run** `pio test -e native` → all pass. Build check only if the owner asks: `pio run -e heltec-v4 && pio run -e heltec-v4-expansion` → SUCCESS for both.
- [ ] **Step 6: Commit** `feat: Heltec V4 expansion build with LVGL health, feed and activity pages`.

---

### Task 7: Release, flash and build scripts, README

**Files:**
- Modify: `.github/workflows/build.yml`, `scripts/release.sh`, `scripts/flash.sh`, `scripts/build-upload-monitor.sh`, `README.md`

- [ ] **Step 1:** `build.yml`: add `pio run -e heltec-v4-expansion`. `release.sh`: `RELEASE_ENVS=(heltec-v4 heltec-v4-expansion)`. The existing per-env loop already names the files `camillia-chat-server-<env>-<tag>.bin`.
- [ ] **Step 2:** `build-upload-monitor.sh`: add `--expansion|-X`, which sets `ENV_NAME="heltec-v4-expansion"`, and update `show_usage`.
- [ ] **Step 3:** `flash.sh`: if no file is given and the `camillia-chat-server-*.bin` files cover more than one board (the text between `camillia-chat-server-` and `-v`), print them and `Pick one: ./flash.sh <file>`, then exit 1. Check by hand: in a temp dir with dummy `camillia-chat-server-heltec-v4-v1.0.0.bin` and `camillia-chat-server-heltec-v4-expansion-v1.0.0.bin`, `./flash.sh` exits 1 and lists both. With only one board present, it behaves as before. Update the header comment and usage to name both boards.
- [ ] **Step 4:** README: add a "Hardware" section with a two-row table: board → env → release image name. Note that the expansion uses the TFT and the plain board uses the OLED.
- [ ] **Step 5: Commit** `build: release and flash the Heltec V4 expansion build`.

---

## Self-review notes

- **Spec coverage:**
  - §4 → T6
  - §5.1 → T2
  - §5.2 → T3, T5
  - §5.3 → T4
  - §6.1–6.6 → T6
  - §7 → T5
  - §8 → T1
  - §9: display failure and touch failure → T6; name table full → T2; activity ring full → T3; settings rejection → T1
  - §10 → native tests in T1–T5; build checks in T5 and T6; the hardware list is run by the owner
  - §11 → T7
- **Owner-run on hardware after T7:** spec §10 items 1–6.
