# Customize and Community Presets for the Chat Server — Design

**Date:** 2026-10-24
**Status:** Draft for review
**Builds on:** `2026-10-02-camillia-chat-server-design.md`. Settings, storage, web config and the YAML backup work as described there unless this document says otherwise.
**Repos affected:** `camillia-chat-server` (firmware) and `camillia-mt-web` (flasher). `camillia-mt` stays read-only: nothing in it changes.

## 1. Purpose

camillia-mt and its web flasher already support customization: the flasher's Customize dialog (and Flash and Customize) pre-configures a device over USB serial with the `cfg` CLI, and community presets (ChiMesh, MichMesh, Freq51, …) can be chosen in the flasher, on the device and in web config. Bring the same concepts to the chat server.

**Success looks like:**
- On the chat server's page, the flasher offers Customize and Flash and Customize. The user sets the radio, channels, names, home WiFi, MQTT ingest and a boot logo, or starts from a community profile. After the flash, the server restarts already configured.
- In the chat server's web config, the user picks a community preset, sees what it will change, confirms, and the server restarts on that mesh with its message history cleared.
- One list of community profiles (`PROFILES` in camillia-mt-web's `src/customize.js`) feeds the MT flasher, MT firmware, CS flasher and CS firmware.

## 2. Scope

**In scope**
- A `cfg` serial CLI in the chat server, compatible with camillia-mt's (§3).
- A boot logo on the LVGL builds' splash (§3.4).
- Generated community presets in the firmware and a picker in web config (§4).
- The CS page's flasher with Customize, product-aware dialog, MQTT block (§5).
- An optional structured `mqtt` field on `PROFILES` (§5.3).

**Out of scope**
- A preset picker on the device's own screen. The status screens stay read-only.
- A logo on the OLED (plain V4) and e-paper (T-Deck Pro) builds.
- Logo upload in web config.
- Any change to camillia-mt, including using the new `mqtt` field there.
- Changes to the YAML export/import.

## 3. Firmware: the `cfg` CLI

### 3.1 Module

New `src/cfg_cli.{h,cpp}`. Parsing, staging and validation are pure C++ (no Arduino), so they are unit-tested natively like `settings_yaml`. The device side (serial I/O, NVS for the logo, commit side effects) is passed in as callbacks.

`serialLoop()` in `main.cpp` passes every complete line starting with `cfg ` to the module and prints its reply. `yaml-export`, `yaml-import` and the test-build commands are unchanged. Any other non-empty line now gets `[cli] unknown command: <first word>`, which the flasher already recognises. The serial line buffer is capped at 256 bytes, the same as MT, and longer lines are dropped with `[cfg] err line too long`.

### 3.2 Staging

The first staging command copies the live `Settings` into a staged copy. Each `set` checks its own field and replies `[cfg] ok <field> staged` or `[cfg] err <field>: <reason>`. Nothing reaches NVS before `commit`. `abort` drops the staged copy and any partial logo and replies `[cfg] ok aborted`.

Arguments keep their case. Text values are percent-encoded by the flasher and decoded byte for byte.

### 3.3 Commands

| Command | Reply / effect |
|---|---|
| `cfg caps` | `[cfg] caps v=1 product=cs firstboot=0 keys=preset,slot,channels,setup,mqtt[,logo] channels=20 presets=<names> [logo=WxH logobg=RRGGBB logomax=8192]` |
| `cfg get preset` | `[cfg] preset=<Name>` |
| `cfg set preset <Name>` | Matched case-insensitively against `kPresets` channel names. Presets this radio cannot run (the Tiny pair without a TCXO) are refused and not listed in `presets=`. |
| `cfg set slot <n>` | 1-based, `0` = the preset's own. Must fit the staged region's plan. |
| `cfg set region <code>` | A `kRegions[].code`. |
| `cfg set longname\|shortname <%enc>` | ≤ 24 / ≤ 4 bytes. |
| `cfg set ssid\|wifipass <%enc>` | ≤ 32 / ≤ 63 bytes. A bare `cfg set wifipass` is an open network. |
| `cfg channels clear` | Empties the staged channel list: `[cfg] ok channels cleared slots=20`. |
| `cfg set channel <i> role=primary\|secondary name=<%enc> psk=default\|none\|<base64> [pos= up= down=]` | Sets slot `i` (0-19, in order, with no gaps). Name ≤ 11 bytes. An empty primary name means "named after the preset", as on MT. `pos`, `up` and `down` are accepted and ignored, so MT-style lines work. |
| `cfg set mqtt on\|off` | `mqttEnabled`. |
| `cfg set mqtt host\|user\|pass\|root <%enc>`, `cfg set mqtt port <n>` | Fit the existing `Settings` field sizes. |
| `cfg logo begin <bytes>` / `data <hex>` / `end <crc32>` / `clear` | LVGL builds only (§3.4). |
| `cfg commit` | See §3.5. |
| `cfg abort` | See §3.2. |

`role` is not supported: the chat server's role is fixed. `caps` does not list it and `cfg set role` replies `[cfg] err role: not supported`.

`firstboot` is always `0`. The chat server has no on-device onboarding, so nothing is held back.

### 3.4 Boot logo

On LVGL builds, `cfg logo` transfers a PNG of at most 8192 bytes in hex chunks, checked by CRC-32 on `end`. It is stored in NVS namespace `cs_logo` on commit, or removed by `clear`. The splash in `status_display_lvgl.cpp` decodes it with LVGL's PNG decoder and draws it in the camellia's place. If it is missing or fails to decode, the camellia is drawn.

`caps` reports the logo box (the camellia's area for the board's layout tier) and the splash card colour as `logobg`, so the flasher fits and flattens the image as it does for MT. OLED and e-paper builds leave `logo` out of `keys` and omit `logo=`. Their `cfg logo …` replies `[cfg] err logo: no logo on this display`.

### 3.5 Commit

`cfg commit`:
1. With nothing staged: `[cfg] ok nothing`.
2. Otherwise runs `settingsValidateDevice()` on the staged copy. On failure: `[cfg] err commit: <reason>`. Staging is kept so the flasher's `abort` cleans up.
3. Saves the settings (and logo), clears every channel's history (`clearMessages(-1)`: new epochs, so nodes' cursors fall back cleanly), replies `[cfg] ok committed rebooting` and restarts after 500 ms.

History is always cleared on commit. A customization usually moves the server to a different mesh, and the old history belongs to the old channels.

## 4. Firmware: community presets

### 4.1 Generated table

New `tools/gen_community_presets.mjs`, adapted from camillia-mt's, reads `PROFILES` from `../camillia-mt-web/src/customize.js` (or a path argument). It writes `src/community_presets.{h,cpp}`, which are committed. The structs match MT's, plus an optional MQTT block:

```cpp
struct CommunityPresetMqtt {
    const char *host;   // nullptr = the profile has no MQTT block
    uint16_t port;
    const char *user;
    const char *pass;
    const char *root;
};
```

`CommunityPreset` gains `CommunityPresetMqtt mqtt`. Logos are dropped, as on MT.

### 4.2 Applying

`communityPresetApply(idx)` turns a profile into `cfg` lines and runs them through the CLI handler: `set region`, `set preset` and `set slot` (if the profile sets them), `channels clear` and one `set channel` per channel, then MQTT only if the profile has an `mqtt` block (`set mqtt on`, then host, port, user, pass, root). Then `commit`. Validation, saving, clearing history and the restart are the CLI's own.

`communityPresetProblem(idx)` returns why a profile cannot be applied on this server (a preset its radio cannot run, a slot outside the region's plan), or nullptr. Channel count never limits a profile here: the server has 20 slots, and no profile has more than 10.

### 4.3 Web config

The radio section gets a **Community Preset** dropdown, grouped by `area`. Profiles with a problem are disabled, with the reason shown.

Choosing one shows, before anything changes:
- the region, radio preset and slot;
- the channel list (names and key type, not key values);
- the MQTT host and root topic, if the profile has them (never the password);
- the community's notes and link;
- the warning: **"This replaces every channel and clears all stored message history."**

**Apply** posts the profile `id` to a new `POST /preset` route. An unknown id or a problem gives a 400 with the reason. Otherwise the route applies the preset (§4.2) and the page shows "Restarting…".

## 5. Flasher (camillia-mt-web)

### 5.1 Turning it on

`CsPage.jsx` renders `<Flasher product={CS_FIRMWARE} devices={CS_DEVICES} customize />`.

### 5.2 A product-aware dialog

`customize.js` gains a per-product description that `Customize.jsx` reads:

| | MT | CS |
|---|---|---|
| Max channels | 10 | 20 |
| Per-channel position / MQTT up / down, and the "All channels" box | shown | hidden |
| Setup: region, names, WiFi | shown | shown |
| Setup: role | shown | hidden |
| MQTT ingest block | — | shown |
| Logo | shown | shown (applied only if `caps` lists `logo`) |
| localStorage key | `camillia.customize` | `camillia.customize.cs` |
| Product name in messages | Camillia-MT | Camillia Chat Server |

The MQTT ingest block holds on/off, host, port, user, password and root topic. As with the WiFi password, the MQTT password is never saved in the browser.

`applyCustomization()` gains one step before commit. When the MQTT block is set and `caps` lists `mqtt`, it sends `cfg set mqtt on|off` and then each non-empty field. When the block is set but `caps` does not list `mqtt`, it fails with the usual "flash a newer release" message. When the device's `caps` says `product=cs`, it sends no `role`. Every other step already follows `caps`.

After a commit on the chat server, the result text reads: "The server restarts. Join `camillia-cs-<short name>` or find it on your WiFi."

### 5.3 `PROFILES` gains `mqtt`

An optional `mqtt: { host, port, user, pass, root }` is added to each profile whose notes give a broker with a shared public login (for example ChiMesh, Iowa Mesh, MeshSTL, LZMesh, Puget Mesh, CT Mesh, NWI, Boston, Central Valley, nyme.sh, BayMesh, SoCal, MBUG, Mesh Hessen, Portugal, creativo.hu meshes, Meshat.se). Each one is checked against the profile's notes. Brokers with per-user logins (Florida Mesh) get no block. The notes keep their text, because MT users still read it.

On the CS page, choosing a profile also fills in the MQTT block. On the MT page the field is ignored, and camillia-mt's generator ignores it too, so its generated files do not change.

## 6. Errors

- Old chat server firmware does not answer `cfg caps`. The flasher's probe times out with "The device did not answer…", and the old-firmware message names Camillia Chat Server.
- A failed apply sends `cfg abort` (existing flasher behaviour), so nothing half-sent is committed later.
- An invalid staged setting is refused at its `set`, with the field and reason, before anything is saved.
- A logo with a bad CRC or over 8192 bytes is refused at `end` / `begin`, and the staged logo is dropped.

## 7. Testing

**Native unit tests (`pio test -e native`)**
- `test_cfg_cli`: each `set` (valid, out of range, bad encoding); `channels clear` and `set channel` order and limits; `pos/up/down` ignored; `role` refused; `caps` line per build flavour (with and without a logo); `commit` with nothing staged, invalid and valid; `abort`; logo begin/data/end with good and bad CRC and size.
- `test_community_presets`: every generated profile applied to default settings either commits valid settings or has a `communityPresetProblem()`; MQTT is changed only for profiles with a block.

**Flasher**
- `npm run build` and a manual check of the CS dialog: hidden MT-only fields, 20 channels, the MQTT block, separate saved state.

**Hardware** (check the board's USB MAC before every flash)
- Primary target: Elecrow CrowPanel Advance 3.5" (`crowpanel-35`, the user's test board). First check its `caps` line, including the logo box for its layout tier. Then Flash and Customize with a community profile, a logo and home WiFi, and confirm the splash logo, channels, MQTT ingest connecting and empty history. Finally, choose a different preset in web config and confirm the confirmation text, the restart and the new channel plan.
- Optional: plain Heltec V4 (OLED). Confirm `cfg caps` has no logo and `cfg logo begin` is refused.

## 8. Release order

1. A chat server firmware release with the `cfg` CLI and presets.
2. Then the camillia-mt-web change that turns on Customize on the CS page and adds `mqtt` to `PROFILES`. After that, re-run `tools/gen_community_presets.mjs` in this repo whenever `PROFILES` changes.

The firmware's generated presets will include MQTT only once step 2's `PROFILES` change exists. To get around that ordering, the `PROFILES` `mqtt` additions can land in camillia-mt-web first, on their own (they do nothing on MT), and the Customize-on-CS change after the firmware release.
