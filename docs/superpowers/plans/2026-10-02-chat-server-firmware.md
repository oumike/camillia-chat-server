# Camillia Chat Server Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Firmware for a plain Heltec V4 that stores up to 3 Meshtastic channels heard on LoRa (and optionally MQTT) and replays missed messages to camillia-mt nodes over the camillia chat server protocol.

**Architecture:** Hardware-free logic (`cs_proto`, `channel_store`, `cs_server`) is plain C++ with no Arduino includes and is unit-tested in a PlatformIO `native` env. Thin Arduino glue (radio ingest, MQTT, web, OLED, persistence I/O) wires that logic to the hardware and is verified on the device. The Meshtastic layer is borrowed from camillia-mt into `lib/camillia_mesh/`.

**Tech Stack:** PlatformIO, `espressif32@7.0.1`, Arduino framework, RadioLib ^7.7.1, rweather/Crypto ^0.4.0, PubSubClient ^2.8, ThingPulse `esp8266-oled-ssd1306` ^4.6.1, LittleFS, Unity (native tests).

**Spec:** `docs/superpowers/specs/2026-10-02-camillia-chat-server-design.md`

## Global Constraints

- Work on branch `server-v1` in this repo; never commit to `main` directly.
- Do not modify `../camillia-mt` in this plan. Its client is a separate plan on its `chat-server` branch.
- Name: "camillia chat server" in UI, logs and code. Never "store and forward"; never use port 65.
- Protocol: Meshtastic `Data.portnum = 256`; payload byte 0 = version `1`, byte 1 = type; little-endian; total payload ≤ 233 bytes.
- Message types: `DISCOVER 0x01`, `ANNOUNCE 0x02`, `REQUEST 0x03`, `BATCH 0x04`. BATCH flags: bit0 `LAST`, bit1 `MORE`, bit2 `TIME_VALID`. Unknown age = `0xFFFFFFFF`.
- Discovery channel: name `camillia-cs`, fixed 16-byte key (Task 2 defines it; the client must copy the same bytes).
- Channel ID = first 4 bytes of `SHA-256(name || key)`, read little-endian.
- Store: 3 channels max (`CS_MAX_CHANNELS = 3`), 250 messages each (`CS_MSGS_PER_CHANNEL = 250`), text ≤ 200 bytes.
- Server: FIFO request queue of 24; max hops default 7; batch size default 10; packet gap default 3 s.
- Persist: when dirty and (60 s elapsed or 20 new messages).
- MQTT: subscribe only. The firmware must contain no MQTT publish call.
- Hardware: plain Heltec WiFi LoRa 32 V4. Radio: SX1262 with SCK 9, MISO 11, MOSI 10, CS 8, DIO1 14, RST 12, BUSY 13, FEM power 7, FEM enable 2, FEM TX mode 46. OLED: SSD1306 128×64 at I2C `0x3C`, SDA 17, SCL 18, RST 21, Vext GPIO 36 (LOW = on). Confirm the OLED pins against the board silkscreen/schematic in Task 1; the radio pins come from camillia-mt's working V4 map.

## Review Focus

1. **Text with multi-byte UTF-8 near 200 bytes.** It must never be cut mid-character when stored or packed. Test: `test_store_truncates_on_utf8_boundary` (Task 3).
2. **A request whose cursor is ahead of the store** (the server's history was trimmed or replaced, but the epoch matched). It must fall through to anchor, then everything, never send nothing forever. Test: `test_since_cursor_beyond_head_falls_back` (Task 4).
3. **The same message heard on LoRa and then MQTT, or relayed with a different hop count.** It must be stored exactly once. Test: `test_store_dedupes_relayed_and_mqtt_copies` (Task 3).
4. **A corrupt or truncated persistence file at boot,** for example after a power cut mid-write. It must start that channel empty with a new epoch, not crash or load garbage. Test: `test_store_rejects_corrupt_blob` (Task 3).
5. **The same node sending a second REQUEST while its first is still queued** (for example a 30-second continuation racing the original). It must replace the queued entry, not double-serve. Test: `test_queue_replaces_duplicate_requester` (Task 5).

---

## File Structure

```
platformio.ini
partitions_16mb_fs.csv              copied from camillia-mt
lib/camillia_mesh/                   borrowed Meshtastic layer (Task 1)
  VENDORED.md
  config.h  debug_flags.h           shims replacing camillia-mt's headers
  mesh_proto.{h,cpp}  mesh_radio.{h,cpp}  xeddsa.{h,cpp}
  mesh_channel_plan.{h,cpp}         presets/regions + slot frequency (from config_io.cpp)
src/
  board.h                           V4 pins (constants above)
  cs_proto.{h,cpp}                  pure: protocol encode/decode, channel ID
  channel_store.{h,cpp}             pure: ring buffers, dedupe, seq, serialize
  cs_server.{h,cpp}                 pure: discovery/request handling, queue, batching
  settings.{h,cpp}                  Settings struct + NVS load/save
  radio_link.{h,cpp}                radio rx/tx using camillia_mesh
  persistence.{h,cpp}               LittleFS read/write of channel blobs
  mqtt_ingest.{h,cpp}
  node_identity.{h,cpp}
  web_ui.{h,cpp}
  status_display.{h,cpp}
  main.cpp
test/
  test_cs_proto/  test_channel_store/  test_cs_server/
```

---

### Task 1: Project scaffold and borrowed mesh layer

**Files:**
- Create: `platformio.ini`, `partitions_16mb_fs.csv`, `src/board.h`, `src/main.cpp`, everything under `lib/camillia_mesh/`

**Interfaces:**
- Produces: `MeshRadio Radio` with `init()`, `reconfigure(freq,bw,sf,cr,power)`, `pollRx(MeshPacket&)`, `transmit(buf,len)`; `decryptPacket`, `encryptPayload`, `computeChannelHash`, `nextMeshPacketId`, `decodeServiceEnvelope`, `CHANNEL_KEYS[]` (all unchanged from camillia-mt `9796a7a`); `kPresets`, `kRegions`, `regionSlotFreq(region, bwKhz, channelName)`.

- [ ] **Step 1: Create branch** `git switch -c server-v1`.
- [ ] **Step 2: `platformio.ini`** with two envs:
  - `heltec-v4`: platform, board, flash/partition and PSRAM settings copied from camillia-mt `[env:heltec-v4]`, **without** its extra_scripts, LVGL, touch, NimBLE, spell and UI flags. Flags `-DBOARD_HAS_PSRAM -DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1 -DRADIOLIB_EXCLUDE_APRS=1 -std=gnu++17 -Os`. lib_deps per Tech Stack. `build_src_filter = +<*>`.
  - `native`: `platform = native`, `test_framework = unity`, `build_src_filter = +<cs_proto.cpp> +<channel_store.cpp> +<cs_server.cpp>`, `lib_ignore = camillia_mesh`, `-std=gnu++17`. Include `test_build_src = yes`.
- [ ] **Step 3: Copy** `mesh_proto`, `mesh_radio`, `xeddsa`, `mesh_channel_plan.h` from camillia-mt `9796a7a` into `lib/camillia_mesh/`. Move `kPresets`, `kRegions`, `kRegionCount`, `presetUsableOnThisRadio`, `regionSlotFreq`, `regionSlotCount`, `kBwCodes`, `loraBwFromCode` definitions out of camillia-mt `src/config_io.cpp` into `mesh_channel_plan.cpp`. Delete the non-V4 `#if DEVICE_*` branches in `mesh_radio.cpp`, keeping the generic SX1262 + FEM path.
- [ ] **Step 4: Write shims.** `config.h` defines `DEVICE_HELTEC_V4_EXPANSION 1` only if `mesh_radio.cpp` needs it to select the FEM path (prefer replacing those `#if`s), includes `../../src/board.h`, and defines `MESH_CHANNELS 4` (3 store channels + discovery), `MAX_CHANNELS 4`, `MESH_TEXT_MAX_LEN 200`, `MESH_POWER 22`, `MESH_HOP_LIMIT 7`, `MY_LORA_RX_BOOST 1`, `MY_HW_MODEL MESH_HW_MODEL_HELTEC_V4`, `MESH_NEIGHBOR_MAX` (camillia-mt's value). `debug_flags.h` maps `debugLogMessages` and the other debug calls `mesh_proto.cpp` uses to `Serial.printf`. `main.cpp` defines the externs `myPubKey`, `myPrivKey` (zeros; PKI unused) and `myDeviceRole = 0`.
- [ ] **Step 5: `VENDORED.md`** lists every copied file, the source commit `9796a7a`, and each local edit (one line each).
- [ ] **Step 6: Radio smoke test in `main.cpp`.** Power the FEM, `Radio.init()`, `Radio.reconfigure(...)` from `regionSlotFreq("US", LongFast bw, "LongFast")` and the LongFast preset params; print each received packet's from, id, and channel hash.
- [ ] **Step 7: Build** `pio run -e heltec-v4`. Expected: SUCCESS.
- [ ] **Step 8: On the device:** first a full flash erase, once: `pio run -e heltec-v4 -t erase`. Then `pio run -e heltec-v4 -t upload && pio device monitor`. Expected: packet lines appear while a nearby LongFast node transmits. Also light the OLED (Vext LOW, reset pulse on 21, "hello" on SSD1306 at 0x3C) to confirm the OLED pins; fix `board.h` if it stays dark.
- [ ] **Step 9: Commit** `feat: scaffold V4 firmware with borrowed camillia mesh layer`.

---

### Task 2: `cs_proto` — protocol encode/decode

**Files:**
- Create: `src/cs_proto.h`, `src/cs_proto.cpp`, `test/test_cs_proto/test_main.cpp`

**Interfaces:**
- Produces (namespace `csp`):
  - `constexpr uint8_t VERSION = 1; enum Type : uint8_t { DISCOVER=1, ANNOUNCE=2, REQUEST=3, BATCH=4 };`
  - `constexpr uint8_t FLAG_LAST=1, FLAG_MORE=2, FLAG_TIME_VALID=4; constexpr uint32_t AGE_UNKNOWN=0xFFFFFFFF; constexpr size_t MAX_PAYLOAD=233; constexpr size_t MAX_TEXT=200;`
  - `extern const char DISCOVERY_CHANNEL_NAME[]; // "camillia-cs"` and `extern const uint8_t DISCOVERY_KEY[16];` (pick 16 random bytes once with `openssl rand -hex 16`, hard-code them, and note them in the spec's §4.2)
  - `struct AnnounceChannel { uint32_t id; char name[12]; };`
  - `struct Announce { char shortName[5]; uint8_t count; AnnounceChannel ch[3]; };`
  - `struct Request { uint32_t epoch, cursor, anchorFrom, anchorId; };`
  - `struct Item { uint32_t seq, from, packetId, ageSec; uint8_t textLen; char text[201]; };`
  - `struct BatchHeader { uint32_t epoch; uint8_t flags; uint32_t serverTime; uint8_t count; };`
  - `uint32_t channelId(const char *name, const uint8_t *key, size_t keyLen);` — needs a SHA-256: use mbedtls on device, and a small bundled implementation (`src/sha256_portable.{h,cpp}`, public-domain) under `#ifndef ARDUINO`.
  - `bool peekType(const uint8_t*, size_t, Type&);`
  - `size_t encodeDiscover(uint8_t*, size_t);`
  - `size_t encodeAnnounce(const Announce&, uint8_t*, size_t); bool decodeAnnounce(const uint8_t*, size_t, Announce&);`
  - `size_t encodeRequest(const Request&, uint8_t*, size_t); bool decodeRequest(const uint8_t*, size_t, Request&);`
  - `size_t itemWireSize(const Item&); // 17 + textLen`
  - `size_t encodeBatch(const BatchHeader&, const Item*, uint8_t n, uint8_t*, size_t);` `bool decodeBatch(const uint8_t*, size_t, BatchHeader&, Item*, uint8_t cap, uint8_t &n);`
  - All encoders return 0 if the buffer is too small; all decoders return false on wrong version or type, or on truncated or overrunning input.

- [ ] **Step 1: Write failing tests:**
  - `test_round_trip_each_type`: encode then decode `Announce` (2 channels, names "LongFast"/"camillia"), `Request{7,42,0xA1B2C3D4,99}`, and `Batch` with 3 items; all fields are equal after the round trip.
  - `test_header_bytes`: `encodeDiscover` yields exactly `{0x01,0x01}`; `encodeRequest` yields length 18, and bytes 2..5 are the epoch little-endian.
  - `test_max_item_fits`: 1 item with a 200-byte text: `encodeBatch` length = 2+4+1+4+1+17+200 = 229 ≤ 233.
  - `test_decode_rejects`: version 2 → false; truncated request (17 bytes) → false; batch whose count says 3 but holds 2 → false; item `textLen` 201 → false.
  - `test_channel_id_stable`: `channelId("LongFast", {0x01}, 1)` equals a hard-coded value computed once with Python `hashlib.sha256(b"LongFast"+b"\x01").digest()[:4]` read little-endian; changing one key byte changes it.
- [ ] **Step 2: Run** `pio test -e native -f test_cs_proto`. Expected: FAIL (undefined symbols).
- [ ] **Step 3: Implement** `cs_proto.cpp`.
- [ ] **Step 4: Run** the same command. Expected: all PASS.
- [ ] **Step 5: Commit** `feat: camillia chat server protocol codec`.

---

### Task 3: `channel_store` — ring buffers, dedupe, persistence format

**Files:**
- Create: `src/channel_store.h`, `src/channel_store.cpp`, `test/test_channel_store/test_main.cpp`

**Interfaces:**
- Consumes: nothing hardware-specific. Allocation goes through a function pointer so the device can use PSRAM: `using AllocFn = void*(*)(size_t);` (default `malloc`; the device passes `ps_malloc`).
- Produces:
  - `constexpr int CS_MAX_CHANNELS = 3; constexpr int CS_MSGS_PER_CHANNEL = 250;`
  - `struct StoredMsg { uint32_t seq, from, packetId, rxUnix, rxUptimeSec; uint8_t source; /*0 LoRa,1 MQTT*/ uint8_t textLen; char text[201]; };`
  - `class ChannelStore` (one channel):
    - `bool begin(AllocFn, uint32_t (*randomFn)());` creates an empty store with a new random epoch
    - `bool add(uint32_t from, uint32_t packetId, const char *text, size_t len, uint32_t rxUnix, uint32_t rxUptimeSec, uint8_t source);` returns false for duplicates (same `from` + `packetId` anywhere in the ring); truncates to ≤200 bytes on a UTF-8 boundary; assigns `seq = lastSeq + 1`
    - `uint32_t epoch() const; uint32_t headSeq() const; uint32_t tailSeq() const; int count() const;`
    - `int findSeqAfterAnchor(uint32_t from, uint32_t packetId) const;` returns that message's seq, or 0 if not held
    - `int copyAfter(uint32_t afterSeq, StoredMsg *out, int max) const;` returns messages with seq > afterSeq, oldest first
    - `void reset(uint32_t newEpoch);`
    - `bool dirty() const; uint32_t addsSinceSave() const; void markSaved();`
    - `size_t serialize(uint8_t *out, size_t cap) const; bool deserialize(const uint8_t *in, size_t len);` Format: magic `"CSS1"`, epoch, lastSeq, count, records, CRC32 trailer. `deserialize` returns false and leaves the store untouched on a bad magic, length or CRC.

- [ ] **Step 1: Write failing tests:**
  - `test_add_assigns_increasing_seq`: 3 adds → seq 1,2,3; `headSeq()==3`.
  - `test_store_dedupes_relayed_and_mqtt_copies`: add (from=A,id=1, source LoRa) then (A,1, source MQTT) → second returns false, count 1.
  - `test_ring_drops_oldest`: 260 adds → count 250, `tailSeq()==11`, `headSeq()==260`; adding a duplicate of the evicted seq-1 message (same from/id) is **accepted** (it's no longer held).
  - `test_store_truncates_on_utf8_boundary`: 199 ASCII bytes + "é" (2 bytes) → stored length 199.
  - `test_copy_after_and_anchor`: after 5 adds, `copyAfter(2,...)` returns seq 3,4,5; `findSeqAfterAnchor` of msg 2 → 2; of an unknown message → 0.
  - `test_serialize_round_trip`: 30 adds → serialize → a fresh store deserializes → same epoch, head, and texts.
  - `test_store_rejects_corrupt_blob`: flip one byte in a serialized blob → `deserialize` false, the store stays empty.
  - `test_dirty_tracking`: add 20 → `addsSinceSave()==20`, `dirty()`; `markSaved()` → clean.
- [ ] **Step 2: Run** `pio test -e native -f test_channel_store`. Expected: FAIL.
- [ ] **Step 3: Implement** `channel_store.cpp` (fixed-size ring of `StoredMsg`, allocated once in `begin`).
- [ ] **Step 4: Run.** Expected: all PASS.
- [ ] **Step 5: Commit** `feat: per-channel message store with dedupe and persistence format`.

---

### Task 4: `cs_server` — since-selection and batch building

**Files:**
- Create: `src/cs_server.h`, `src/cs_server.cpp`, `test/test_cs_server/test_main.cpp`

**Interfaces:**
- Consumes: `ChannelStore` (Task 3), `csp::*` (Task 2).
- Produces:
  - `uint32_t selectStart(const ChannelStore&, const csp::Request&);` returns the seq to send *after*. Rules in spec §4.4 order: epoch matches and `tailSeq()-1 <= cursor <= headSeq()` → cursor; else anchor found → anchor seq; else `tailSeq()-1` (everything).
  - `struct BatchPlan { int count; bool more; };`
  - `BatchPlan planBatch(const ChannelStore&, uint32_t afterSeq, int batchSize, StoredMsg *out);`
  - `int packItems(const StoredMsg *msgs, int n, uint32_t nowUnix, bool timeValid, uint32_t nowUptimeSec, uint32_t epoch, bool moreAfterBatch, uint8_t packets[][csp::MAX_PAYLOAD], size_t *lens, int maxPackets);` greedily packs items into packets in order; sets `LAST` on the final packet, and `MORE` on it only if `moreAfterBatch`; age = `nowUnix - rxUnix` when both are valid, else `nowUptimeSec - rxUptimeSec` if the message was stored this boot, else `AGE_UNKNOWN`.

- [ ] **Step 1: Write failing tests:**
  - `test_since_uses_cursor_when_epoch_matches`
  - `test_since_uses_anchor_when_epoch_differs`
  - `test_since_everything_when_nothing_matches` → returns `tailSeq()-1`
  - `test_since_cursor_beyond_head_falls_back`: epoch matches, cursor = head+5 → falls to anchor/everything, never returns ≥ head with count 0
  - `test_plan_batch_sets_more`: 25 msgs after start, batch 10 → count 10, more true; the third call → count 5, more false
  - `test_pack_respects_payload_limit`: 10 items of 150-byte text → every `lens[i] <= 233`, all 10 items present in order across packets, only the last packet has `LAST`
  - `test_pack_age_unknown_after_reboot`: a message with `rxUnix==0` from a previous boot → `AGE_UNKNOWN`
- [ ] **Step 2: Run** `pio test -e native -f test_cs_server`. Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Expected: PASS.
- [ ] **Step 5: Commit** `feat: chat server since-selection and batch packing`.

---

### Task 5: `cs_server` — request queue, hop rules, pacing state machine

**Files:**
- Modify: `src/cs_server.h`, `src/cs_server.cpp`, `test/test_cs_server/test_main.cpp`

**Interfaces:**
- Consumes: Task 4 functions.
- Produces:
  - `struct ServerConfig { uint8_t maxHops = 7; uint8_t batchSize = 10; uint16_t packetGapMs = 3000; char shortName[5]; };`
  - `struct Outgoing { uint32_t to; int chanSlot; /*-1 = discovery channel*/ uint8_t hopLimit; uint8_t payload[csp::MAX_PAYLOAD]; size_t len; };`
  - `class CsServer`:
    - `void begin(const ServerConfig&, ChannelStore *stores, const uint32_t *chanIds, const char (*chanNames)[12], int chanCount);`
    - `void onPacket(uint32_t from, int chanSlot, uint8_t hopsTravelled, const uint8_t *payload, size_t len, uint32_t nowMs);` handles DISCOVER (on slot -1, or unicast on any slot) by queueing an ANNOUNCE and REQUEST (on a store slot) by enqueueing. It drops packets with `hopsTravelled > maxHops`.
    - `bool poll(uint32_t nowMs, uint32_t nowUnix, bool timeValid, uint32_t nowUptimeSec, Outgoing &out);` returns at most one packet per call, and only when `nowMs >= nextSendMs`; after a BATCH packet `nextSendMs = nowMs + packetGapMs`. ANNOUNCEs go ahead of batches.
    - `int queueLength() const; bool busy() const;`
  - Queue: FIFO, capacity 24. A REQUEST from a node already queued for the same slot **replaces** that entry in place. When full, new requests are dropped.
  - Reply hop limit = `min(hopsTravelled, maxHops)`.

- [ ] **Step 1: Write failing tests:**
  - `test_discover_yields_announce`: DISCOVER from node X at 2 hops → `poll` gives `Outgoing{to=X, chanSlot=-1, hopLimit=2}`, which decodes to an Announce listing the configured channels.
  - `test_ignores_beyond_max_hops`: maxHops 3, request at 4 hops → `queueLength()==0`.
  - `test_request_paced_by_gap`: 25 stored, batchSize 10, gap 3000 → the first packet comes at t=0, the next `poll` at t=2999 returns false, and at t=3000 returns true; the final packet of the batch has `MORE`.
  - `test_queue_capacity_24`: 25 distinct requesters → `queueLength()==24`.
  - `test_queue_replaces_duplicate_requester`: X requests slot 0 twice while queued → `queueLength()==1`, and the second request's cursor is the one served.
  - `test_serves_fifo_one_at_a_time`: requests from X then Y → every packet for X is emitted before any for Y.
- [ ] **Step 2: Run.** Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `pio test -e native`. Expected: all suites PASS.
- [ ] **Step 5: Commit** `feat: chat server request queue and pacing`.

---

### Task 6: Settings and web config

**Files:**
- Create: `src/settings.{h,cpp}`, `src/web_ui.{h,cpp}`

**Interfaces:**
- Produces:
  - `struct ChannelCfg { char name[12]; uint8_t key[32]; uint8_t keyLen; };`
  - `struct Settings { char shortName[5]; char longName[25]; char region[12]; uint8_t modemPreset; uint8_t freqSlot; /*0 = auto*/ uint8_t chanCount; ChannelCfg ch[3]; uint8_t batchSize; uint16_t packetGapMs; uint8_t maxHops; bool mqttEnabled; char mqttHost[64]; uint16_t mqttPort; char mqttUser[32]; char mqttPass[64]; char mqttRoot[48]; char apPass[64]; char staSsid[33]; char staPass[64]; };`
  - `void settingsDefaults(Settings&, uint32_t nodeId);` (short name = last 4 hex digits of the node ID; region "US"; LongFast; 1 channel "LongFast" key `{0x01}`; batch 10; gap 3000; hops 7; MQTT off; root "msh/US"; port 1883)
  - `bool settingsLoad(Settings&); void settingsSave(const Settings&);` NVS namespace `cs`, one blob plus a version byte.
  - `void webBegin(Settings*, std::function<void(const Settings&, const Settings&)> onSaved, std::function<String()> statusJson);` `void webLoop();`
- Web: an `ESPAsyncWebServer`-free plain `WebServer` on port 80; AP SSID `camillia-cs-<short>`. One HTML form for every field in spec §5.5 (key entered as base64, like Meshtastic URLs), a Save button, and a status block polling `/status` every 5 s.
- Validation on save: chanCount 1–3; batchSize 1–50; gap 500–60000 ms; maxHops 0–7; channel names non-empty and unique; keys decode to 1, 16 or 32 bytes (1 byte expands with camillia_mesh `expandPsk`).

- [ ] **Step 1: Implement** settings and the web UI, wired in `main.cpp` (AP always on; STA joins when `staSsid` is set; NTP via `configTime` once STA is up).
- [ ] **Step 2: Build.** `pio run -e heltec-v4`. Expected: SUCCESS.
- [ ] **Step 3: On the device:** join the AP and open `http://192.168.4.1`. Change the short name and add a 2nd channel; reboot; the values persist. Submit batch size 0 → rejected with a message, nothing saved.
- [ ] **Step 4: Commit** `feat: settings in NVS and web config`.

---

### Task 7: LoRa ingest, persistence, OLED — the server stores live traffic

**Files:**
- Create: `src/radio_link.{h,cpp}`, `src/persistence.{h,cpp}`, `src/status_display.{h,cpp}`, `src/node_identity.{h,cpp}`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `Settings`, `ChannelStore`, camillia_mesh.
- Produces:
  - `bool radioBegin(const Settings&);` applies the preset/region/slot (slot 0 = `regionSlotFreq` hash of the preset's channel name). Fills `CHANNEL_KEYS[0..chanCount-1]` from settings and the last slot with the discovery channel.
  - `bool radioPoll(MeshPacket &pkt);` returns decrypted packets only.
  - `bool radioSend(uint32_t to, int chanSlot, uint8_t hopLimit, uint32_t portnum, const uint8_t *payload, size_t len);` (`chanSlot -1` = discovery)
  - `uint8_t hopsTravelled(const MeshHdr&);` = `hop_start - hop_limit`, or 0 if `hop_start == 0`
  - `void persistLoadAll(ChannelStore*, const Settings&);` `void persistMaybeSave(ChannelStore*, int n, uint32_t nowMs);` Files `/cs/ch<i>.bin`, written to `.tmp` and then renamed. A channel whose name/key hash differs from the one saved in `/cs/ch<i>.meta` is reset with a new epoch.
  - `void identityBegin(const Settings&);` `void identityLoop(uint32_t nowMs);` NODEINFO at boot, then every 3 h, on each store channel.
  - `struct DisplayStatus { const char *nodeName; char ip[16]; const char *mqtt; uint32_t lastSentUnix; uint32_t lastSentUptimeSec; bool everSent; };`
  - `void displayBegin(); void displayUpdate(const DisplayStatus&);` lines, top to bottom: `Camillia Chat Server`, long name, IP (`AP 192.168.4.1` when only the AP is up), `MQTT: <state>`, `Sent: <YYYY-MM-DD HH:MM | 12m ago | never>`. Plain 10px font, redraw at most once a second.
  - `CsServer` gains `bool lastSentAt(uint32_t &unix, uint32_t &uptimeSec) const;` updated on every BATCH packet sent (Task 8 feeds it into `DisplayStatus`).
- Ingest rule: `pkt.portnum == TEXT_MESSAGE_APP`, `pkt.hdr.to == 0xFFFFFFFF`, `chanIdx` is a store slot → `store[chanIdx].add(...)`.

- [ ] **Step 1: Implement and wire into `main.cpp`.**
- [ ] **Step 2: Build.** Expected: SUCCESS.
- [ ] **Step 3: On the device:**
  - Send 5 messages on LongFast from a camillia-mt node; the OLED count goes to 5, and `/status` shows them.
  - Re-send one message via a relay; the count does not increase.
  - Power-cycle 70 s after the last message; the count is still 5 after boot.
  - The node list on the camillia-mt node shows the server by its short name within a minute of the server booting.
  - The OLED shows the five lines; changing the long name in web config updates line 2 after saving.
- [ ] **Step 4: Commit** `feat: store live LoRa traffic with flash persistence and OLED status`.

---

### Task 8: Serve discovery and sync over LoRa

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `CsServer` (Task 5), `radioPoll`/`radioSend`/`hopsTravelled` (Task 7).

- [ ] **Step 1: Wire it up.** Port-256 packets go to `CsServer::onPacket`. Each loop, `while (server.poll(...)) radioSend(out.to, out.chanSlot, out.hopLimit, 256, out.payload, out.len)`. `timeValid` = `time(nullptr) > 1700000000`.
- [ ] **Step 2: Add a serial test hook** (`#ifdef CS_SERIAL_TEST`, off by default). It accepts the line `req <slot> <cursor>`, which feeds a fake REQUEST from node `0x12345678` into `onPacket`, so the batching can be seen before the camillia-mt client exists.
- [ ] **Step 3: On the device,** with `-DCS_SERIAL_TEST` and 25 stored messages:
  - `req 0 0` → the serial log shows 10 items over ~4 packets about 3 s apart, with `MORE` on the last.
  - `req 0 <last seq>` → 10 more.
  - The OLED `Sent:` line updates to the current time (or `0m ago`).
- [ ] **Step 4: Region duty-cycle guard.** When the region is `EU_868` or `EU_433`, track rolling one-hour airtime (from RadioLib `getTimeOnAir`). Hold the packet whenever sending it would take the hour above 10%; `poll` waits. Verify by setting the region to EU_868 with a 1-message gap and seeing sends pause in the log.
- [ ] **Step 5: Commit** `feat: serve chat server discovery and sync over LoRa`.

---

### Task 9: MQTT ingest

**Files:**
- Create: `src/mqtt_ingest.{h,cpp}`
- Modify: `src/main.cpp`

**Interfaces:**
- Produces: `void mqttBegin(const Settings&, std::function<void(const MeshHdr&, const uint8_t*, size_t, const char*)> onEnvelope);` `void mqttLoop(uint32_t nowMs);` `const char *mqttState();` (`"off"`, `"no wifi"`, `"connecting"`, `"connected"`)
- Subscribes to `<root>/2/e/<channel name>/#` for each store channel. Decodes each with `decodeServiceEnvelope`, then decrypts with `decryptPacket` and ingests with the same rule as Task 7 (source = MQTT). Reconnects with backoff of 5 s, doubling up to 5 min.
- Contains no `publish` call; a `grep -n publish src/mqtt_ingest.cpp` must return nothing.

- [ ] **Step 1: Implement and wire.**
- [ ] **Step 2: Build;** run `grep -n "publish" src/mqtt_ingest.cpp`. Expected: build SUCCESS; grep prints nothing.
- [ ] **Step 3: On the device,** with STA and MQTT configured against a broker carrying the channel:
  - A message published by another gateway appears in the store (source MQTT on `/status`).
  - The same message also heard on LoRa → stored once.
  - Stop the broker → state "connecting", and LoRa ingest continues.
- [ ] **Step 4: Commit** `feat: optional receive-only MQTT ingest`.

---

## Self-review notes

- Spec coverage: §4 protocol → T2/T4/T5/T8; §5.3 store → T3/T7; §5.4 identity → T7; §5.5 web → T6; §5.6 OLED → T7; §5.7 MQTT → T9; duty cycle → T8; §8 native tests → T2–T5, hardware checks → T1/T6–T9. §6 (camillia-mt client) is deliberately excluded and is a separate plan on camillia-mt's `chat-server` branch.
- Bring-up items 2, 3 and 6 in spec §8 need the client; they are run in the client plan's final task.
