# Chat Server Customize and Community Presets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The chat server accepts camillia-mt's `cfg` serial protocol, draws a custom boot logo, offers generated community presets in its web config, and the web flasher's Customize dialog works on the CS page.

**Architecture:** A pure `cfg_cli` module (natively tested) stages a copy of `Settings` and a logo buffer. Device specifics come in through a hooks struct, as `YamlPresetMap` does for YAML. `main.cpp` feeds it serial lines and carries out commits (save, clear history, restart). Community presets are generated from camillia-mt-web's `PROFILES` and applied as `cfg` lines through the same module. The flasher becomes product-aware and turns on Customize for CS.

**Tech Stack:** C++17 / Arduino-ESP32 / PlatformIO / Unity (native tests) / LVGL 9.5 (lodepng) / Node (generator) / React + Vite (flasher).

**Spec:** `docs/superpowers/specs/2026-10-24-cs-customize-presets-design.md`

## Global Constraints

- Reply wording matches camillia-mt: `[cfg] ok <field> staged`, `[cfg] err <field>: <reason>`, `[cfg] ok aborted`, `[cfg] ok nothing`, `[cfg] ok committed rebooting`, `[cli] unknown command: <word>`.
- `caps`: `[cfg] caps v=1 product=cs firstboot=0 keys=preset,slot,channels,setup,mqtt[,logo] channels=20 presets=<names>[ logo=WxH logobg=100C29 logomax=8192]`.
- Logo ≤ 8192 bytes, NVS namespace `cs_logo`, hex chunks, CRC-32 (zlib polynomial, as the flasher's `crc32()`).
- Serial line cap 256 bytes; longer → `[cfg] err line too long`.
- Commit always clears all history; restart 500 ms after the reply.
- An empty primary channel name means "named after the staged preset" (`kPresets[preset].channelName`).
- `role` → `[cfg] err role: not supported`. `pos/up/down` accepted and ignored.
- camillia-mt: no changes. camillia-mt-web: work on a local branch `cs-customize`, not pushed.
- Never flash. Build freely.

## Review Focus

- A flasher that sends `cfg set channel` before `cfg set preset` (community preset with an empty name): the primary must still be named after the final staged preset. Resolve names at commit. Test in Task 1.
- A second `cfg channels clear` mid-apply, or channels sent out of order or with a gap: refuse a gap (`i != count`). Test in Task 1.
- Commit fails validation (e.g. two channels named the same): staged state is kept, so `abort` still answers `ok aborted`. Test in Task 1.
- `cfg logo data` before `begin`, or past the announced size: error, and the logo stays unstaged. Test in Task 2.
- A percent-encoded value with a bad escape (`%G1`, trailing `%`): `err <field>: bad encoding`. Test in Task 1.

---

### Task 1: `cfg_cli` staging core

**Files:**
- Create: `src/cfg_cli.h`, `src/cfg_cli.cpp`, `test/test_cfg_cli/test_main.cpp`
- Modify: `platformio.ini` (`[env:native]` build_src_filter adds `+<cfg_cli.cpp>`)

**Interfaces:**
- Produces:
  ```cpp
  struct CfgHooks {
      int  (*presetFromName)(const char *name);   // -1 unknown or unusable on this radio
      const char *(*presetName)(uint8_t preset);  // kPresets[].channelName
      bool (*regionKnown)(const char *code);
      bool (*validate)(const Settings &s, char *err, size_t cap);  // settingsValidateDevice + persistFits
      const char *presetList;                     // comma list for caps
      bool hasLogo; uint16_t logoW, logoH;
  };
  enum class CfgResult { Reply, Commit };          // Commit: caller saves staged(), clears history, restarts
  class CfgCli {
   public:
      CfgCli(const CfgHooks &h);
      // line without the leading "cfg ". Writes one reply line (no newline) to out.
      CfgResult handle(const char *line, const Settings &live, char *out, size_t cap);
      const Settings &staged() const;
      bool logoStaged() const; bool logoClear() const;
      const uint8_t *logoData() const; size_t logoLen() const;
      void reset();                                // after a commit is carried out
  };
  bool cfgPercentDecode(const char *in, char *out, size_t cap);   // false on bad escape or overflow
  ```

- [ ] **Step 1: Write failing tests** in `test/test_cfg_cli/test_main.cpp` with fake hooks (presets LongFast=0, MediumFast=4; regions US, EU_868; validate = `settingsValidate`; hasLogo false):
  - `caps` → exact string `[cfg] caps v=1 product=cs firstboot=0 keys=preset,slot,channels,setup,mqtt channels=20 presets=LongFast,MediumFast`
  - `set preset mediumfast` → `[cfg] ok preset=MediumFast staged`; `set preset Nope` → starts with `[cfg] err preset:`
  - `set slot 51` ok; `set slot 256` err; `set region EU_868` ok; `set region XX` err
  - `set longname Hill%20Top` → staged longName `"Hill Top"`; 25-byte name → err; `set shortname %G1` → `[cfg] err shortname: bad encoding`
  - `set ssid Home` and bare `set wifipass` → staPass `""`
  - `channels clear` → `[cfg] ok channels cleared slots=20`; `set channel 0 role=primary name= psk=default pos=1 up=1 down=0` ok; `set channel 2 ...` after only 0 → err (gap); `set channel 1 role=secondary name=Chicago psk=H1HDyEqg5W9XFilicVTIaoWYrs8URmWGo5fAYfbxQgo=` ok, keyLen 32; `psk=none` keyLen 0; 12-byte name err
  - primary named after preset: channels set, then `set preset MediumFast`, `commit` → `CfgResult::Commit`, `staged().ch[0].name == "MediumFast"`, `ch[0].key == {1}`
  - `set mqtt on`, `set mqtt host mqtt.chimesh.org`, `set mqtt port 1883`, `set mqtt root msh%2FUS%2FIL%2FChi` → staged fields; `set mqtt port 0` err
  - `set role CLIENT` → `[cfg] err role: not supported`
  - `commit` with nothing staged → `[cfg] ok nothing`, Reply
  - duplicate channel names, `commit` → starts with `[cfg] err commit:`, then `abort` → `[cfg] ok aborted`
  - `get preset` → `[cfg] preset=LongFast`
  - unknown sub-command `frob` → `[cfg] err frob: unknown`
- [ ] **Step 2: Run** `pio test -e native -f test_cfg_cli`. Expected: FAIL (no cfg_cli.h).
- [ ] **Step 3: Implement** `cfg_cli.{h,cpp}`. Staging starts on the first `set`/`channels` by copying `live`. Channels are staged into a local list with a `namedAfterPreset` flag and written into the staged Settings at commit, after resolving names. `psk=default` → key `{0x01}` len 1, otherwise `parseKeyBase64`. Commit runs `hooks.validate` on the resolved copy.
- [ ] **Step 4: Run** `pio test -e native -f test_cfg_cli`. Expected: PASS, and `pio test -e native` all pass.
- [ ] **Step 5: Commit** `cfg CLI: staging core with native tests`.

### Task 2: Logo transfer, storage and splash

**Files:**
- Modify: `src/cfg_cli.{h,cpp}`, `test/test_cfg_cli/test_main.cpp`, `src/status_display.h`, `src/status_display_lvgl.cpp`, `src/lv_conf.h` (`LV_USE_LODEPNG 1`)
- Create: `src/logo_store.{h,cpp}` (device only)

**Interfaces:**
- Produces: `uint32_t cfgCrc32(const uint8_t *p, size_t n);` · `bool logoLoad(std::vector<uint8_t> &png); void logoSave(const uint8_t *p, size_t n); void logoErase();` (NVS `cs_logo`, key `png`) · `bool displayLogoBox(uint16_t &w, uint16_t &h);` in `status_display.h` (false on OLED and e-paper builds, which define it to return false).

- [ ] **Step 1: Failing tests** (hooks with `hasLogo=true, logoW=200, logoH=60`): caps ends ` logo=200x60 logobg=100C29 logomax=8192` and keys include `logo`. `logo begin 8193` → err. `logo data 00` before begin → err. begin 3 / data `89504e` / end with `cfgCrc32` of those bytes in 8-digit hex → `[cfg] ok logo staged`, `logoStaged()`. Wrong CRC → err and `!logoStaged()`. Data past the announced size → err. `logo clear` → ok, `logoClear()`. With `hasLogo=false`, `logo begin 3` → `[cfg] err logo: no logo on this display`. A staged logo alone makes `commit` return Commit.
- [ ] **Step 2: Run** `pio test -e native -f test_cfg_cli`. Expected: FAIL.
- [ ] **Step 3: Implement** the logo commands in cfg_cli (buffer is a `std::vector<uint8_t>`), `logo_store.cpp` with Preferences, and in `buildSplash()`: if `logoLoad()` gives a PNG, show it with `lv_image` from an `lv_image_dsc_t` (PNG source; keep the bytes alive until `endSplash()`), centred where the camellia goes, instead of the canvas. `displayLogoBox` = width `kW - 40`, height `(kH - 50) - titleEnd - 8`, from the same layout as `buildSplash()`. Implement `displayLogoBox` returning false in `status_display_oled.cpp` and `status_display_epaper.cpp`.
- [ ] **Step 4: Run** the native tests, then `pio run -e heltec-v4-expansion -e heltec-v4 -e crowpanel-35`. Expected: PASS / SUCCESS.
- [ ] **Step 5: Commit** `cfg CLI: boot logo transfer; splash draws a stored logo`.

### Task 3: Wire `cfg` into the serial console

**Files:**
- Modify: `src/main.cpp` (`serialLoop`, hooks, commit), `README.md` (new "USB serial setup" section)

**Interfaces:**
- Consumes: Task 1/2 `CfgCli`, `CfgHooks`, `logoSave/logoErase`, `displayLogoBox`.
- Produces: `static bool applyCfgLine(const char *line, char *reply, size_t cap)` in main.cpp. Task 5 calls it to apply presets.

- [ ] **Step 1: Implement.** Build `CfgHooks` from `presetIndex`, `presetName`, `regionKnown`, `settingsValidateDevice` plus `persistFits`, and the usable preset list. `serialLoop` caps `line` at 256 bytes (then `[cfg] err line too long`), passes `cfg …` to `applyCfgLine`, and answers other unknown words with `[cli] unknown command: <word>` (the test build keeps its `req`/`disc`). On `Commit`: `clearMessages(-1)` while the old `s_settings` is still live, save the logo (or erase it), `settingsSave(staged)`, print the reply, then `s_restartAtMs = millis() + 500`.
- [ ] **Step 2: Build** every env: `pio run` (P4 envs with `PLATFORMIO_CORE_DIR=~/.platformio-p4`). Expected: all SUCCESS.
- [ ] **Step 3: Commit** `cfg CLI on the USB serial console`.

### Task 4: `PROFILES` gains `mqtt` (camillia-mt-web, branch `cs-customize`)

**Files:**
- Modify: `../camillia-mt-web/src/customize.js`

- [ ] **Step 1:** Add `mqtt: { host, port, user, pass, root }` to every profile whose `notes` give a broker with a shared login, copying the values from the notes verbatim (port 1883 when the notes give none). Skip per-user logins (Florida). Document the field in the comment above `PROFILES`.
- [ ] **Step 2: Verify** `npm run build` succeeds, and a node one-liner lists each profile with `mqtt` next to its notes for a by-eye comparison.
- [ ] **Step 3: Commit** in camillia-mt-web `Customize: MQTT brokers as data on community profiles`.

### Task 5: Generated community presets in the firmware

**Files:**
- Create: `tools/gen_community_presets.mjs`, `src/community_presets.{h,cpp}` (generated), `test/test_community_presets/test_main.cpp`
- Modify: `src/main.cpp`, `platformio.ini` (native filter `+<community_presets.cpp> +<community_preset_lines.cpp>`)
- Create: `src/community_preset_lines.{h,cpp}` (pure: profile → cfg lines)

**Interfaces:**
- Produces: the structs from spec §4.1 · `int communityPresetLines(int idx, char lines[][160], int max);` (cfg lines without `cfg `, ending with `commit`) · `const char *communityPresetProblem(int idx);` (device, main.cpp) · `bool communityPresetApply(int idx, char *err, size_t cap);` (device: runs the lines through `applyCfgLine`, stops at the first `err`, then `abort`).

- [ ] **Step 1:** Write the generator, adapted from camillia-mt's, emitting the `mqtt` block. Run `node tools/gen_community_presets.mjs`. Expected: the two files, with ChiMesh's `mqtt.host == "mqtt.chimesh.org"`.
- [ ] **Step 2: Failing test:** for every `kCommunityPresets[i]`, feed `communityPresetLines(i)` into a `CfgCli` with fake hooks that accept every preset name in the table. Each line must reply `ok`, and `commit` must return Commit. ChiMesh's staged settings have `mqttEnabled` and host `mqtt.chimesh.org`. A profile without `mqtt` leaves `mqttHost` as it was.
- [ ] **Step 3: Run** `pio test -e native -f test_community_presets`. Expected: FAIL. Then implement `community_preset_lines.cpp` (percent-encode names and MQTT values). Expected: PASS.
- [ ] **Step 4:** Implement `communityPresetProblem` (preset unusable on this radio; region unknown) and `communityPresetApply` in main.cpp. `pio run -e heltec-v4 -e crowpanel-35`. Expected: SUCCESS.
- [ ] **Step 5: Commit** `Community presets, generated from the flasher's profiles`.

### Task 6: Web config picker and `POST /preset`

**Files:**
- Modify: `src/web_ui.{h,cpp}`, `src/main.cpp`

**Interfaces:**
- Consumes: `kCommunityPresets`, `communityPresetProblem`, `communityPresetApply` via a `webSetPresets(std::function<bool(int, char*, size_t)> apply, std::function<const char*(int)> problem)` hook.

- [ ] **Step 1: Implement.** In the Radio card, add a `<select id="cp">` grouped by `<optgroup label=area>`, with problem profiles `disabled` and the reason in the label. Add a details panel filled in JS from an inline JSON array: id, area, label, url, region, preset, slot, channels (name plus key type: default/none/custom), mqtt host/root, notes (no keys, no MQTT password). Show the warning "This replaces every channel and clears all stored message history." and an Apply button that posts `id` to `/preset`. `/preset` returns 400 with the reason, or the existing "Restarting…" page. The device path applies the lines, whose commit clears history, saves and restarts.
- [ ] **Step 2: Build** `pio run -e heltec-v4 -e crowpanel-35`. Expected: SUCCESS. Render-check: run the page HTML through a quick `node --check` on the extracted script.
- [ ] **Step 3: Commit** `Web config: Community Preset picker`.

### Task 7: Flasher Customize on the CS page (camillia-mt-web, branch `cs-customize`)

**Files:**
- Modify: `../camillia-mt-web/src/customize.js`, `src/components/Customize.jsx`, `src/components/Flasher.jsx`, `src/components/CsPage.jsx`, `README.md`

**Interfaces:**
- Produces: `export const PRODUCTS = { mt: {...}, cs: {...} }` with `maxChannels, channelFlags, role, mqtt, storageKey, name`. `loadCustomization(product)`, `saveCustomization(settings, product)`, and `applyCustomization(port, settings, log, product)` take it. `EMPTY_CUSTOMIZATION` gains `mqtt: null` (`{enabled, host, port, user, pass, root}`).

- [ ] **Step 1: Implement** per spec §5.2: Flasher passes `product` to Customize, CsPage sets `customize`, hide pos/up/down and role for CS, add the MQTT block (password never stored), apply the MQTT step after setup when `caps.keys` has `mqtt`, skip `role` for `product=cs`, choosing a profile fills `mqtt`, use CS wording in the old-firmware and result messages.
- [ ] **Step 2: Verify** with `npm run build` and `npm run lint` if present. Expected: success.
- [ ] **Step 3: Commit** in camillia-mt-web `Customize on the chat server page`.

### Task 8: Docs and full verification

- [ ] **Step 1:** README: "Customize from the web flasher" and "Community presets" sections. Add a RELEASE_NOTES entry in the file's existing style.
- [ ] **Step 2:** `pio test -e native` and `pio run` (all envs, P4 with its core). Expected: all pass.
- [ ] **Step 3: Commit** `Docs: Customize and community presets`.
