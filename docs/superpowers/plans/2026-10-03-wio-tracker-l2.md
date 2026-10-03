# Wio Tracker L2 Build Target Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `wio-tracker-l2` env that runs the chat server on the Seeed Wio Tracker L2 with the same three LVGL pages, the Wake button acting as a tap, and no change to either Heltec build.

**Architecture:** First a pure refactor. Board-specific display code moves behind `display_hal.h`, early power-up behind `board_init.h`, and the battery ADC into `battery_adc.cpp`, with one file per board selected by `build_src_filter`. Then the Wio files are added against those interfaces, copied from camillia-mt where it already works on this board.

**Tech Stack:** PlatformIO, espressif32@7.0.1 (Arduino core 2.x), LVGL 9.5.0, LovyanGFX (Heltec `^1.1.16`, Wio exactly `1.2.27`), Adafruit ADS1X15 2.6.2.

**Spec:** `docs/superpowers/specs/2026-10-03-wio-tracker-l2-design.md`, which builds on `docs/superpowers/specs/2026-10-03-heltec-v4-expansion-display-design.md`.

**Reference (read-only):** `../camillia-mt`:
- `src/hal/hw_wio_tracker_l2.h`
- `src/hal/xl9555.{h,cpp}`
- `src/hal/wio_tracker_l2_io.{h,cpp}`
- `src/hal/display.h` (the `DEVICE_WIO_TRACKER_L2` branches)
- `src/battery_util.cpp` (ADS1115)
- `boards/wio-tracker-l2.json`
- `src/config.h` (`MESH_HW_MODEL_SEEED_WIO_TRACKER_L2`)
- `platformio.ini` `[env:wio-tracker-l2]`

## Global Constraints

- Builds are authorized: run `pio run -e <env>` for every env a task touches, plus `pio test -e native`. **Do not upload, erase or open a serial port.** The controller flashes the hardware.
- After Task 1, `heltec-v4`, `heltec-v4-test` and `heltec-v4-expansion` must behave exactly as before. Code moves, but behaviour does not change.
- Wio libs: `lovyan03/LovyanGFX@1.2.27` (exact), `lvgl/lvgl@9.5.0`, `adafruit/Adafruit ADS1X15@2.6.2`. Partitions: `partitions_16mb_fs.csv`.
- Wio I2C: `Wire`, SDA 47, SCL 48, 100 kHz. Expander 0x21 (INT GPIO45), LP5814 0x2C, GT911 0x5D, ADS1115 0x48. Hardware model 137 on the Wio and 110 everywhere else.
- Wio expander outputs left **off**: Grove (7), GNSS power (13), audio PA (12), SD power (14), USB-OTG (11), LED (10), battery sense (15, except during a read). GNSS reset (9) stays asserted.
- Copied files carry the first line `// Vendored from camillia-mt <short-commit>: <path>; edit there, not here.` (`git -C ../camillia-mt rev-parse --short HEAD`).
- Commit trailer, exactly:
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_01AEipdDWzsjDnCozgZUronb`
- Branch `server-v1`; do not push.

**Decided here (spec gap):** `boardEarlyInit()` runs before `Serial.begin`, so anything it wants to log is printed by a second call, `boardReport()`, made right after `Serial.begin` + `delay(1500)`.

## Review Focus

1. **The Heltec expansion silently changing behaviour after the split:** for example touch-failure rules, backlight-off-on-failure, rotation, or the GPIO36/GPIO7 order. *Check: Task 1, Step 4 diffs each moved block against its original, and the controller reflashes the Heltec.*
2. **The Wio expander not answering** (loose board, wrong address). Expected: the server runs without a screen and logs it once; there must be no hang, no I2C storm and no crash. *Check: Task 2, Step 3 makes `dhalBegin` fail with "expander not found" while `boardExpanderOk()` is false.*
3. **The Wake button being held down, or bouncing.** Expected: one tap per press, nothing repeating while it's held. *Check: Task 3 `wakeEdge` is a pure helper with a native test.*
4. **The GT911 probe leaving the I2C bus stuck,** so later LP5814 or ADS1115 writes time out. Expected: the post-GT911 `Wire.end()/Wire.begin(47, 48, 100000)` reset, in mt's order. *Check: Task 3, Step 3 ordering.*
5. **The ADS1115 missing, or the battery not connected.** Expected: battery shows as absent and nothing is logged repeatedly. *Check: Task 2 `battery_ads1115.cpp` logs once at begin, and `batteryVolts()` returns 0.*

---

### Task 1: Board-specific code behind interfaces (no behaviour change)

**Files:**
- Create: `src/board_init.h`, `src/board_init_v4.cpp`, `src/board_init_v4_exp.cpp`, `src/display_hal.h`, `src/display_hal_heltec_exp.cpp`
- Rename: `src/battery.cpp` → `src/battery_adc.cpp`
- Modify: `src/main.cpp` (setup), `src/status_display_lvgl.cpp`, `platformio.ini`

**Interfaces:**
- Produces `board_init.h`:
  ```cpp
  void boardEarlyInit();   // first statement of setup(), before Serial
  void boardReport();      // after Serial.begin; logs what boardEarlyInit found (may print nothing)
  ```
- Produces `display_hal.h`:
  ```cpp
  bool dhalBegin(char *err, size_t cap);                                   // panel + backlight + touch
  void dhalFlush(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *px);
  bool dhalTouch(int16_t &x, int16_t &y);                                  // false = released or touch off
  void dhalBacklight(uint8_t level);
  void dhalOff();                                                          // display failed after begin: backlight 0 if panel is up
  bool dhalWakePressed();                                                  // one true per press; Heltec: always false
  ```
- [ ] **Step 1: Move code.**
  - `board_init_v4_exp.cpp` gets the current `#if defined(CS_BOARD_V4_EXPANSION)` block from `setup()`, with its comments, verbatim. `boardReport()` there is empty.
  - `board_init_v4.cpp` has two empty functions.
  - `main.cpp` calls `boardEarlyInit()` as the first statement and `boardReport()` after `delay(1500)`.
  - `display_hal_heltec_exp.cpp` gets the `LGFX_V4Exp` instance, `s_panelUp`, the panel init (rotation, brightness 160, fillScreen), the touch probe with retries, the 3-slow-reads touch rule, `pushImage` flush and backlight. It reports init failures through `err`.
  - `status_display_lvgl.cpp` keeps LVGL, the splash, pages and cycler. It calls `dhalBegin` (on false: `fail(err)`), `dhalFlush` in `flushCb`, `dhalTouch` in `touchReadCb`, `dhalBacklight` where it set brightness, and `dhalOff()` in `fail()` in place of `s_lcd.setBrightness(0)`. Each `displayUpdate`, after `tick()`: if `dhalWakePressed()`, call `s_cycler.tap(now)`, but only once the splash is over, like taps.
  - It no longer includes `hal_v4_exp_display.h` or LovyanGFX.
  - `git mv src/battery.cpp src/battery_adc.cpp` with no content change.
- [ ] **Step 2: Filters.**
  - `heltec-v4`: `+<*> -<status_display_lvgl.cpp> -<display_hal_*.cpp> -<board_init_v4_exp.cpp> -<board_init_wio_l2.cpp> -<battery_ads1115.cpp>`.
  - `heltec-v4-expansion`: `+<*> -<status_display_oled.cpp> -<display_hal_wio_l2.cpp> -<board_init_v4.cpp> -<board_init_wio_l2.cpp> -<battery_ads1115.cpp>`.
  - Excluding files that don't exist yet is harmless.
- [ ] **Step 3: Build** `pio test -e native` → all pass. `pio run -e heltec-v4 -e heltec-v4-test -e heltec-v4-expansion` → three SUCCESS.
- [ ] **Step 4: Prove nothing changed.** In the report, put each moved block's old lines (from `git show HEAD~:<file>`) next to its new location, and list every line that differs other than identifiers renamed for the interface. There should be none apart from the `dhal*` call sites.
- [ ] **Step 5: Commit** `refactor: board init, display HAL and battery ADC behind per-board files`.

---

### Task 2: Wio env, board header, expander bring-up, battery, hardware model (headless)

**Files:**
- Create: `boards/wio-tracker-l2.json` (copied), `src/board_wio_l2.h`, `src/xl9555.h`, `src/xl9555.cpp` (copied), `src/board_init_wio_l2.cpp`, `src/battery_ads1115.cpp`, `src/display_hal_wio_l2.cpp` (headless for now; Task 3 replaces it)
- Modify: `src/board.h`, `lib/camillia_mesh/config.h`, `platformio.ini`

**Interfaces:**
- Consumes: Task 1's `board_init.h` and `display_hal.h`.
- Produces in `board_wio_l2.h`: the pins and expander bit names from spec §3.1 (`EXP_BIT_*` named exactly as in mt's header), and `bool boardExpanderOk();` (defined in `board_init_wio_l2.cpp`). Also `bool boardExpanderRead(uint16_t &in);` and `void boardExpanderSet(uint8_t bit, bool on);` for the battery and Wake-button code.
- [ ] **Step 1: Board files.**
  - `board.h` gets an `#elif defined(CS_BOARD_WIO_L2)` branch.
  - `config.h`: `#if defined(CS_BOARD_WIO_L2)` sets `MY_HW_MODEL` to 137 (`MESH_HW_MODEL_SEEED_WIO_TRACKER_L2`), else 110.
  - `board_wio_l2.h` defines the SX1262 pins, FEM pins −1 and `MESH_TCXO_V 1.8f`, plus the TFT/LP5814/GT911/ADS1115 constants copied from mt's header.
  - If the battery code needs placeholders for the Heltec ADC macros, define `BATT_ADC_PIN -1`. Better: `battery_ads1115.cpp` doesn't reference them at all.
- [ ] **Step 2: `board_init_wio_l2.cpp`.**
  - `boardEarlyInit()`: `Wire.begin(47, 48, 100000)`, then mt's `wio_tracker_l2_io` boot sequence using the copied `xl9555` driver: latches before directions; LCD power on; LCD and touch reset pulses with mt's delays; LCD-CS bit high; touch-INT bit low; every bit in Global Constraints off, GNSS reset asserted.
  - Store whether the expander answered, and the shadows.
  - `boardReport()` prints `[cs] board: expander 0x21 ok, out=%04x dir=%04x`, or `[cs] board: expander not found at 0x21`.
  - The two expander helpers do nothing (or return false) when the expander is missing.
- [ ] **Step 3: Headless `display_hal_wio_l2.cpp`:** `dhalBegin` returns false with err `"expander not found"` when `!boardExpanderOk()`, otherwise `"wio display not implemented"`. The other functions do nothing or return false. This file only exists until Task 3.
- [ ] **Step 4: `battery_ads1115.cpp`** (implements `battery.h`):
  - `batteryBegin`: `Adafruit_ADS1115::begin(0x48, &Wire)`. If that fails, log `[cs] battery: ADS1115 not found at 0x48` once, and `batteryVolts()` returns 0 from then on.
  - `batteryLoop` every 10 s: expander bit 15 on, `delay(2)`, `readADC_SingleEnded(0)` at `GAIN_TWO`, `computeVolts` × 2.0, bit 15 off. Smoothing exactly as in `battery_adc.cpp`.
- [ ] **Step 5: Env.**
  - Copy `boards/wio-tracker-l2.json` verbatim, with a provenance note in `platformio.ini` (JSON has no comments).
  - `[env:wio-tracker-l2]` extends common, with `board = wio-tracker-l2`, `board_build.arduino.memory_type = qio_opi`, and `build_flags = ${common.build_flags} -DCS_BOARD_WIO_L2=1 -DLV_CONF_INCLUDE_SIMPLE -Isrc`.
  - `lib_deps` = common + the three Wio libs.
  - `build_src_filter = +<*> -<status_display_oled.cpp> -<display_hal_heltec_exp.cpp> -<board_init_v4.cpp> -<board_init_v4_exp.cpp> -<battery_adc.cpp>`.
  - Update the file's header comment to list three device envs.
- [ ] **Step 6: Build** `pio test -e native`, then `pio run -e heltec-v4 -e heltec-v4-test -e heltec-v4-expansion -e wio-tracker-l2` → four SUCCESS, with no new warnings from `src/`.
- [ ] **Step 7: Commit** `feat: Wio Tracker L2 env with expander bring-up, ADS1115 battery and hardware model`.

*(The controller flashes the Wio after this task's review and expects: the expander line, the radio up, a battery value or "not found", and `[cs] display: wio display not implemented`.)*

---

### Task 3: Wio display: NV3031B panel, LP5814 backlight, GT911 touch, Wake button

**Files:**
- Create: `src/hal_wio_l2_display.h` (copied panel/light/touch device class), `src/wake_edge.h`, `src/wake_edge.cpp`
- Modify: `src/display_hal_wio_l2.cpp` (replace the headless version), `platformio.ini` (native filter `+<wake_edge.cpp>`)
- Test: `test/test_wake_edge/test_main.cpp`

**Interfaces:**
- Consumes: `boardExpanderOk/Read` and the `EXP_BIT_*` names from Task 2.
- Produces in `wake_edge.h` (pure):
  ```cpp
  // True exactly once per press. `pressed` is the debounced level (active = true).
  // A level must be stable for >= 30 ms to count; held presses never repeat.
  class WakeEdge { public: bool update(bool pressed, uint32_t nowMs); };
  ```
- [ ] **Step 1: Write the failing tests** for `test_wake_edge`:
  - `test_single_press`: false at 0 ms; pressed at 10, still pressed at 45 gives true once; still pressed at 2000 gives false.
  - `test_bounce_ignored`: pressed at 0, released at 10, pressed at 20, released at 25 gives false throughout.
  - `test_two_presses`: press, then release for ≥30 ms, then press again gives two trues.
  - `test_wraparound`: the same as single press, starting at `0xFFFFFFF0`.
- [ ] **Step 2: Run** `pio test -e native -f test_wake_edge`. Expected: FAIL (missing header).
- [ ] **Step 3: Implement.**
  - `hal_wio_l2_display.h`: an `LGFX_WioL2 : lgfx::LGFX_Device` built from mt's `DEVICE_WIO_TRACKER_L2` branches: `Bus_SPI` in quad mode on SPI3 with the §3.1 pins, mode 3 at 75/16 MHz, `Panel_NV3031B` (240×320, invert, RGB order, offset rotation 1), the `Light_WioTrackerL2LP5814` class verbatim, and `Touch_GT911` (0x5D, I2C port 0, 47/48, 100 kHz, offset rotation 2, no INT or RST).
  - `display_hal_wio_l2.cpp`:
    - `dhalBegin`: if `!boardExpanderOk()`, fail with "expander not found". Otherwise `lcd.init()`, in mt's order: LP5814 before GT911, then `Wire.end(); Wire.begin(47, 48, 100000);` right after. Then rotation 0 (giving 320×240 internally), brightness 160 and fill black.
    - LP5814 init failure: log once and continue; `dhalBacklight` does nothing from then on.
    - GT911: 3 failed reads in a row turns touch off with one log line, the same rule as the Heltec.
    - `dhalWakePressed()`: if GPIO45 is low, or 50 ms have passed since the last read, call `boardExpanderRead(in)`. Active-low bit 0 gives `pressed`; return `s_wake.update(pressed, millis())`.
  - `pinMode(45, INPUT_PULLUP)` in `dhalBegin`.
- [ ] **Step 4: Run** `pio test -e native` → all pass. `pio run -e heltec-v4-expansion -e wio-tracker-l2` → both SUCCESS.
- [ ] **Step 5: Commit** `feat: Wio Tracker L2 display (NV3031B, LP5814, GT911) and Wake button`.

*(The controller flashes the Wio and the Heltec expansion after review. The owner checks both screens.)*

---

### Task 4: CI, release, scripts, README

**Files:** `.github/workflows/build.yml`, `scripts/release.sh`, `scripts/build-upload-monitor.sh`, `scripts/flash.sh` (comment only), `README.md`
- [ ] **Step 1:**
  - `build.yml`: add `pio run -e wio-tracker-l2`.
  - `release.sh`: `RELEASE_ENVS=(heltec-v4 heltec-v4-expansion wio-tracker-l2)`, and add the image to the header comment.
  - `build-upload-monitor.sh`: `--wio|-W` sets `ENV_NAME="wio-tracker-l2"`; update the usage text.
  - `flash.sh`: header comment and usage name the Wio.
  - README: a third row in the Hardware table.
- [ ] **Step 2:** `bash -n` each changed script. In a scratch dir with dummy images for all three boards, `scripts/flash.sh` exits 1 and lists all three.
- [ ] **Step 3: Commit** `build: release and flash the Wio Tracker L2 build`.

---

## Self-review notes
- **Spec coverage:**
  - §4.1 → T1, T3
  - §4.2 → T1, T2
  - §4.3 → T1, T2
  - §4.4 → T2
  - §5 → T1 (Heltec filters), T2 (env)
  - §6 → T3 (backlight mapping, Wake debounce)
  - §7 → T2 (expander, ADS1115), T3 (panel, LP5814, GT911)
  - §8 → builds in every task; hardware by the controller after T2 and T3
  - §9 → T4
- **Interface consistency:** `dhalOff` is added beyond the spec's list, to keep the Heltec "backlight off on failure" behaviour behind the HAL. `boardReport` was decided above.
