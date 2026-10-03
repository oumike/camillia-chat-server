# Wio Tracker L2 Build Target — Design (addendum)

**Date:** 2026-10-03
**Status:** Draft for review
**Builds on:** `2026-10-03-heltec-v4-expansion-display-design.md` (the "expansion spec"). The pages, settings, cycler, name table, activity log and error rules there apply unchanged unless this document says otherwise.
**Repo affected:** `camillia-chat-server` only. `camillia-mt` is a read-only reference.

## 1. Purpose

Add a `wio-tracker-l2` build of the chat server for the Seeed Wio Tracker L2. It behaves exactly like the other builds: radio, store, protocol, web config and MQTT. It shows the expansion build's three rotating pages on its 320×240 screen. The top Wake button acts like a tap.

**Success looks like:** the Wio boots to the splash, then cycles the health, feed and activity pages. A tap or a Wake-button press wakes the screen or moves to the next page. The Heltec V4 expansion build behaves exactly as it does today, and the plain V4 build is unchanged.

## 2. Scope

**In scope**
- New env `wio-tracker-l2`, with a board header, early board init (PCA9555 expander), display back end (NV3031B QSPI panel, LP5814 backlight, GT911 touch) and ADS1115 battery reading.
- Splitting the board-specific display code out of `status_display_lvgl.cpp` behind a small `display_hal` interface, with the Heltec expansion code moved over unchanged.
- Battery reading made per-board.
- Meshtastic hardware model reported per board.
- CI, release, build script and README updates.

**Out of scope**
- GNSS, audio (ES8311/ES7243E), SD card, Grove port, USB-OTG, user LED and BLE. All of them stay powered off through the expander.
- GPIO0 (BOOT/user button): left unused.
- A long press on the Wake button, or turning the screen fully off.
- VNC.

## 3. Reference: camillia-mt

camillia-mt's `env:wio-tracker-l2` runs on the owner's Wio, and its screen, touch and backlight have been confirmed working. Code is copied from there with a provenance first line (`// Vendored from camillia-mt <commit>: <path>; edit there, not here.`):

| What | mt source |
|---|---|
| Pins and comments | `src/hal/hw_wio_tracker_l2.h` |
| Expander driver | `src/hal/xl9555.{h,cpp}` |
| Expander bring-up sequence | `src/hal/wio_tracker_l2_io.{h,cpp}` (only the boot sequence and the bit helpers this build uses) |
| Panel, backlight and touch | `src/hal/display.h`: `Panel_NV3031B` QSPI configuration, `Light_WioTrackerL2LP5814`, `Touch_GT911` configuration, and the I2C reset after the GT911 probe |
| Battery | `src/battery_util.cpp`: ADS1115 path |
| Board definition | `boards/wio-tracker-l2.json` |
| Hardware model | `src/config.h`: `MESH_HW_MODEL_SEEED_WIO_TRACKER_L2 = 137` |

### 3.1 Hardware facts carried over

| Item | Value |
|---|---|
| MCU / memory | ESP32-S3, 16 MB flash, 8 MB octal PSRAM (`qio_opi`) |
| Shared I2C | Wire, SDA 47, SCL 48, 100 kHz. The expander, GT911, LP5814 and ADS1115 all sit on it. |
| Expander | PCA9555 at 0x21, interrupt GPIO45 (active-low). Bits: 0 Wake button (input, active-low), 3 touch INT (driven low), 4 LCD CS (held high after reset), 5 LCD power, 6 LCD reset, 7 Grove power, 8 touch reset, 9 GNSS reset, 10 LED, 11 USB-OTG, 12 audio PA, 13 GNSS power, 14 SD power, 15 battery-sense enable |
| Panel | NV3031B 240×320 on QSPI (SPI3): SCK 42, IO0–IO3 = 41/40/39/38, CS 46, no DC and no reset GPIO, SPI mode 3, 75 MHz write. Invert on, RGB order on, panel offset rotation 1. Logical rotation 0 gives 320×240 landscape. |
| Backlight | LP5814 at 0x2C. mt's register init, then PWM on all 4 channels = brightness |
| Touch | GT911 at 0x5D, polled (INT is an expander bit), offset rotation 2. Initialise the LP5814 before the GT911, and run `Wire.end(); Wire.begin(47, 48, 100000)` after the GT911 init. |
| Battery | ADS1115 at 0x48, AIN0, GAIN_TWO, ×2 divider. Sense enabled by expander bit 15 (active-high). |
| LoRa | SX1262: SCK 4, MISO 5, MOSI 6, CS 21, RST 7, DIO1 9, BUSY 8. No FEM (all FEM pins −1). TCXO 1.8 V on DIO3, DIO2 as the RF switch. |

## 4. Structure

### 4.1 Display split

```cpp
// src/display_hal.h — one implementation per TFT board, chosen by build_src_filter.
bool dhalBegin(char *err, size_t cap);              // panel, backlight, touch; false = display unusable
void dhalFlush(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *px);
bool dhalTouch(int16_t &x, int16_t &y);             // false = released, or touch is off
void dhalBacklight(uint8_t level);
bool dhalWakePressed();                             // true once per press (edge); Heltec: always false
```

- `status_display_lvgl.cpp` keeps the LVGL setup, splash, pages, `PageCycler` and backlight rule. It calls `dhal*` for everything that touches hardware.
- Touch-failure handling moves into each back end. `dhalTouch` returns false once touch is off, and each back end logs the failure once.
- `dhalWakePressed()` is checked in `displayUpdate()`, after `tick()`. A press calls `PageCycler::tap()`.
- `display_hal_heltec_exp.cpp` takes over the existing Heltec code (the `LGFX_V4Exp` device, CHSC6X touch probe with retries, the 3-slow-reads rule, and the backlight going off when the display fails). Its behaviour must not change.
- `display_hal_wio_l2.cpp` holds the new Wio code (§3.1).

### 4.2 Early board init

`void boardEarlyInit();` is the first call in `setup()`, with one implementation per env:
- `board_init_v4.cpp`: does nothing (plain `heltec-v4` and `heltec-v4-test`).
- `board_init_v4_exp.cpp`: the existing VEXT LOW and GPIO7 HIGH lines and their comments, moved out of `main.cpp` unchanged.
- `board_init_wio_l2.cpp`:
  1. `Wire.begin(47, 48, 100000)`.
  2. Run mt's expander sequence: stage the output latches before setting directions; LCD power on; pulse the LCD and touch resets; LCD-CS bit high; touch-INT bit low.
  3. Leave Grove, GNSS power, audio PA, SD power, USB-OTG and the LED off, GNSS reset asserted, and battery sense off.
  4. Log the expander shadows once.

  If the expander doesn't ACK, log `[cs] board: expander not found at 0x21` and set a flag. `dhalBegin()` then fails with "expander not found", and the server runs without a screen.

`main.cpp`'s `#if defined(CS_BOARD_V4_EXPANSION)` block is replaced by the `boardEarlyInit()` call.

### 4.3 Battery

`battery.h` keeps its API (`batteryBegin`, `batteryLoop`, `batteryVolts`).
- `battery_adc.cpp`: today's `battery.cpp`, renamed, for both Heltec envs.
- `battery_ads1115.cpp`: Wio. Every 10 s: sense bit 15 on, settle 2 ms, a single-shot read of AIN0 at GAIN_TWO, ×2, sense off. The same smoothing as `battery_adc.cpp`. If the ADS1115 isn't found at `batteryBegin`, `batteryVolts()` returns 0, which `battery_level` already reports as absent.

### 4.4 Board header and hardware model

- `board.h` gains a `CS_BOARD_WIO_L2` branch for `board_wio_l2.h`, which holds the §3.1 pins and expander bit names, copied from mt's header with comments trimmed to what this build uses.
- `lib/camillia_mesh/config.h`: `MY_HW_MODEL` is 137 when `CS_BOARD_WIO_L2` is defined, and 110 (Heltec V4) otherwise.

## 5. Build

`[env:wio-tracker-l2]` extends `[common]` and overrides:
- `board = wio-tracker-l2`, from `boards/wio-tracker-l2.json` (octal PSRAM, `use_1200bps_touch`, `wait_for_upload_port`).
- `board_build.arduino.memory_type = qio_opi`.
- `board_build.partitions = partitions_16mb_fs.csv`, the chat server's existing table with LittleFS for the store.
- `build_flags`: the common flags plus `-DCS_BOARD_WIO_L2=1 -DLV_CONF_INCLUDE_SIMPLE -Isrc`.
- `lib_deps`: the common libs plus `lovyan03/LovyanGFX@1.2.27` (exact; it is the first version with `Panel_NV3031B` and the one mt runs on this board), `lvgl/lvgl@9.5.0`, and `adafruit/Adafruit ADS1X15@2.6.2`.
- `build_src_filter`: `+<*>`, minus the other boards' files: `status_display_oled.cpp`, `display_hal_heltec_exp.cpp`, `board_init_v4.cpp`, `board_init_v4_exp.cpp` and `battery_adc.cpp`.

The two Heltec envs gain the matching exclusions for the Wio files and for each other's files. `heltec-v4-test` still extends `heltec-v4`. `native` is unchanged.

`lv_conf.h` is shared and unchanged.

## 6. Settings and behaviour

- The four display settings behave as in the expansion spec. Brightness maps straight to the LP5814 PWM value.
- "External power" uses the same rule (`BATT_EXTERNAL` or `BATT_ABSENT`).
- The Wake button is debounced by edge detection on the expander read. The read happens only when GPIO45 is low, or at most every 50 ms as a fallback.

## 7. Error handling

| Failure | Behaviour |
|---|---|
| Expander missing | Logged; no display; the server runs headless |
| Panel init fails | As in the expansion spec: `[cs] display: <reason>` and the display marked off |
| LP5814 missing | Logged once; the display keeps working with the backlight at its power-on level, and `dhalBacklight` does nothing |
| GT911 missing or failing | Logged once; touch off; the Wake button and the timer still work |
| ADS1115 missing | Logged once; battery shown as absent |

## 8. Testing

- **Native:** `pio test -e native` stays green. No new pure logic.
- **Builds:** `heltec-v4`, `heltec-v4-test`, `heltec-v4-expansion` and `wio-tracker-l2` all build with no new warnings from our files.
- **On hardware** (the Wio is connected; the Heltec expansion too):
  1. Wio boot log: expander shadows, LP5814 and GT911 OK, ADS1115 OK, radio up, no `[cs] display:` errors.
  2. The owner confirms the splash, then the pages, the 30 s rotation, a tap advancing, the Wake button advancing or waking, and colours and orientation.
  3. Reflash the Heltec expansion with the split code. Its boot log is unchanged and the owner confirms the screen still works.

## 9. Release

- `build.yml`: add `pio run -e wio-tracker-l2`.
- `release.sh`: add it to `RELEASE_ENVS`, and add its image to the header comment.
- `build-upload-monitor.sh`: `--wio|-W`.
- README hardware table: a third row.
- `flash.sh` needs no change (its board detection already handles any env name). Its header comment names the Wio too.
