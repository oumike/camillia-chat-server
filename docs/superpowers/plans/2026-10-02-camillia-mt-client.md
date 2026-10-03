# camillia-mt Chat Server Client Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** camillia-mt nodes find a camillia chat server, catch up on missed channel messages from it, and show them in their channels, replacing the old Meshtastic Store & Forward client.

**Architecture:**
- The protocol codec (`cs_proto`) is vendored from the server repo.
- All client decisions live in a pure, host-tested state machine (`cs_client`): when to discover, what to request, cursor tracking and notices.
- `main_lvgl.cpp` glue does radio I/O, posting into `ChannelMgr`, persistence and UI, following the file's existing patterns.
- Task 1 first makes a small server change the client depends on.

**Tech Stack:**
- camillia-mt: Arduino/PlatformIO, LVGL, host tests via `tests/run.sh` (system `c++ -std=c++17 -Wall -Wextra -Werror`).
- Server: PlatformIO `native` Unity tests.

**Spec:** `docs/superpowers/specs/2026-10-02-camillia-chat-server-design.md` (camillia-chat-server repo), §4 and §6.

## Global Constraints

- **Branches:**
  - camillia-mt work happens only on branch `chat-server` of `../camillia-mt`. Never touch `main` or `chat-changes`.
  - Server work (Task 1) happens on `server-v1` of camillia-chat-server.
- **Naming:** "camillia chat server" / "Chat Server" in UI, logs and code. Never "store and forward" for the new feature.
- **Protocol** (spec §4):
  - Port 256.
  - Payload ≤ `csp::MAX_PAYLOAD` (231).
  - Discovery channel `camillia-cs` with key `45a51ee6e782493ffb9cdb164bd7cdf7`.
  - Channel match by `csp::channelId(name, key)`.
- **Client timings** (spec §6.3):
  - Automatic mode syncs on boot (once a server is known), then every **15 min**.
  - After `MORE`, re-ask after **30 s**, outside the 15-min limit.
  - **Check now** has a **5 min** cooldown.
  - With no server and mode not Off, DISCOVER every **60 min**: broadcast, hop limit **3**.
  - A reply timeout is **60 s**. **3** unanswered in a row raise the live-screen notice "Can't reach chat server XXXX".
  - The server is **never** forgotten automatically. A manually set server is **never** replaced.
- **Modes:** Automatic (default) / Manual only (no boot sync) / Off.
- **Request hop limit:** `min(hopsToServer + 1, 7)`, or 7 while the distance is unknown.
- **Replayed messages:**
  - Inserted in time order.
  - Skipped if the channel already holds (sender, packet ID).
  - Marked as from the chat server.
- **Dependencies and layout:**
  - No new libraries.
  - Every `RhinoConfig` change keeps the blob layout rules documented in `src/config_io.h`: reuse in place, or take reserved pad bytes.
- **UI coverage:** every config change is reflected on the device UI (`main_lvgl.cpp`), the web UI (`web_config.cpp`) and YAML (`config_io.cpp`), per `docs/AI_WORKFLOW_MAP.md`.

## Review Focus

1. **A replayed message the node already shows** (heard live, or replayed twice) must not appear twice. Test: `test_post_skips_known_message` (Task 4, device check), plus client test `test_batch_items_returned_even_on_gap` (Task 3).
2. **A lost packet in the middle of a batch** must not advance the cursor past the gap. The next request must re-fetch it. Test: `test_gap_stops_cursor` (Task 3).
3. **The server's store was cleared or replaced** (new epoch) while the node holds a cursor. The node must accept the new history, not stall. Test: `test_new_epoch_resets_cursor` (Task 3).
4. **A manually set server that is unreachable** must keep being tried, never replaced by a discovered one, and must raise the notice exactly once. Test: `test_manual_server_unreachable_notice_once_never_replaced` (Task 3).
5. **The node's clock is unset or wrong.** Ages still produce sensible times. The server's time is adopted only when the clock is unset and the time source isn't Manual. Test: `test_display_epoch_from_age` (Task 3).

---

### Task 1 (server repo): contiguous sequence numbers and FLAG_FIRST

**Files:**
- Modify (camillia-chat-server, branch `server-v1`): `src/cs_proto.h`, `src/cs_server.h/.cpp`, `src/channel_store.h/.cpp`, `src/main.cpp`
- Modify tests: `test/test_cs_server/test_main.cpp`, `test/test_channel_store/test_main.cpp`
- Modify spec: §4.3, §4.4

**Interfaces:**
- Produces:
  - `csp::FLAG_FIRST = 8`, set on the first packet of every batch (including an empty one).
  - `ServerConfig::beforeServe`: a `std::function<void(int slot)>`, called before the first packet of a transfer is built.
  - `CS_SEQ_RESERVE` is removed.

- [ ] **Step 1: Write the failing tests.**
  - `test_pack_first_flag_only_on_first_packet`: 10 items of 150 bytes → packet 0 has `FLAG_FIRST`, the others don't. An empty pack → one packet with `FIRST|LAST`.
  - `test_before_serve_called_before_first_packet`: the hook records its slot; after `request(0xA, 1, …)` and `poll` → called once, with slot 1, before the first packet is returned.
  - Rename `test_reload_reserves_sequence_numbers` → `test_reload_keeps_sequence_contiguous`. After a reload, `add` gives `head + 1`.
  - Delete `test_ring_with_seq_gap_drops_oldest`. `deserialize` still accepts increasing seqs, but nothing produces gaps any more.
- [ ] **Step 2:** `pio test -e native`. Expected: FAIL (`FLAG_FIRST`, `beforeServe` undefined).
- [ ] **Step 3: Implement.**
  - Remove `CS_SEQ_RESERVE` from `deserialize`.
  - `packItems` sets `FLAG_FIRST` on packet 0.
  - `startNext` calls `_cfg.beforeServe(slot)` if set.
  - `main.cpp` sets `beforeServe` to `persistSaveNow(store, slot)` when that store is dirty. Every seq a node can see is then on flash, so it is never reused after a power cut. This replaces I4's reserve.
  - Spec:
    - §4.3: add bit3 `FIRST` to the flags.
    - §4.4: add "seqs are contiguous; the server saves a channel before serving it".
- [ ] **Step 4:** `pio test -e native` → all pass. Then `scripts/build-upload-monitor.sh -B`.
- [ ] **Step 5: Flash.** Run `heltec-v4-test`, send `req 1 0`. The serial log shows a BATCH whose header flags include FIRST: add `FIRST` to `logOutgoing`.
- [ ] **Step 6: Commit** `fix: contiguous seqs (save before serving) and FLAG_FIRST for client gap detection`.

---

### Task 2: vendor the protocol codec into camillia-mt

**Files:**
- Create: `src/cs_proto.h`, `src/cs_proto.cpp`, `src/sha256_portable.h`, `src/sha256_portable.cpp` (copied from camillia-chat-server at the Task 1 commit)
- Create: `tests/test_cs_proto.cpp`

**Interfaces:**
- Produces: the `csp::` API exactly as in the server repo.

- [ ] **Step 1: Copy the four files.** Prepend one comment line: `// Vendored from camillia-chat-server <commit>; edit there, not here.`
- [ ] **Step 2: Write `tests/test_cs_proto.cpp`** in the style of `tests/test_admin_proto.cpp` (`ok()` counter, `main` returning non-zero on failure). Add `// test-deps: sha256_portable.cpp`. Cases:
  - Request round trip.
  - Batch round trip with `FLAG_FIRST|FLAG_LAST`.
  - `channelId("LongFast", {0x01}, 1) == 0xf989c0d1`.
  - `encodeAnnounce` of 10 channels ≤ 231 bytes.
- [ ] **Step 3: Run** `tests/run.sh`. Expected: `test_cs_proto` passes under `-Wall -Wextra -Werror`. Fix any warnings in the vendored files *in the server repo first*, then re-copy.
- [ ] **Step 4: Commit** (camillia-mt) `feat: vendor camillia chat server protocol codec`.

---

### Task 3: `cs_client` — the client state machine (pure, host-tested)

**Files:**
- Create: `src/cs_client.h`, `src/cs_client.cpp`, `tests/test_cs_client.cpp` (`// test-deps: cs_proto.cpp sha256_portable.cpp`)

**Interfaces:**
- Consumes: `csp::` (Task 2).
- Produces (namespace `csc`):

```cpp
enum Mode : uint8_t { MODE_OFF = 0, MODE_AUTO = 1, MODE_MANUAL = 2 };
constexpr uint32_t AUTO_INTERVAL_MS = 15UL * 60 * 1000, MORE_DELAY_MS = 30000,
                   CHECK_COOLDOWN_MS = 5UL * 60 * 1000, DISCOVERY_INTERVAL_MS = 60UL * 60 * 1000,
                   REPLY_TIMEOUT_MS = 60000;
constexpr int UNANSWERED_NOTICE = 3, MAX_LOCAL = 10;
constexpr uint8_t DISCOVERY_HOPS = 3, MAX_HOPS = 7;

struct ChannelState { uint32_t epoch, cursor; };          // persisted per local channel
struct Anchor { uint32_t from, packetId; };                // {0,0} = none
struct Send { uint32_t to; int chanIdx; /* -1 = discovery channel */ uint8_t hopLimit;
              uint8_t payload[csp::MAX_PAYLOAD]; size_t len; };
enum Notice : uint8_t { NOTICE_NONE, NOTICE_FOUND, NOTICE_UNREACHABLE, NOTICE_REACHABLE };

class Client {
public:
    // chanIds[i] = csp::channelId of local channel i, 0 = unused slot.
    void begin(Mode mode, uint32_t serverId, bool manual, const uint32_t *chanIds, int nChans,
               const ChannelState *saved, uint32_t nowMs);
    void setMode(Mode m, uint32_t nowMs);
    void setServer(uint32_t nodeId, bool manual, uint32_t nowMs);   // resets all ChannelState
    void clearServer(uint32_t nowMs);                                // resets all ChannelState
    void onAnnounce(uint32_t from, uint8_t hopsTravelled, const csp::Announce &a, uint32_t nowMs);
    // Returns how many of `items` the caller should post (all of them); advances the
    // channel cursor only across contiguous seqs.
    int  onBatch(uint32_t from, int chanIdx, const csp::BatchHeader &h, const csp::Item *items,
                 uint8_t n, uint32_t nowMs);
    bool checkNow(uint32_t nowMs, const char **why);   // why: "off", "no server", "cooldown", "busy"
    bool poll(uint32_t nowMs, const std::function<Anchor(int chanIdx)> &anchorFor, Send &out);
    uint32_t serverId() const; bool manual() const; Mode mode() const; uint8_t hopsToServer() const;
    bool serverHasChannel(int chanIdx) const;
    const ChannelState &state(int chanIdx) const;
    bool takeStatesDirty();   // true once after any ChannelState change
    Notice takeNotice();      // oldest pending notice, or NOTICE_NONE
};
// Display time for an item: nowUnix - ageSec, or 0 when the age is unknown or the clock is unset.
uint32_t displayEpoch(uint32_t nowUnix, bool clockSet, uint32_t ageSec);
```

**Behaviour the tests pin:**
- **Sync is one channel at a time.**
  - A round covers every local channel the server announced, in index order.
  - Each REQUEST waits for its `LAST` packet or the 60 s timeout.
  - `MORE` re-queues the same channel 30 s later. Otherwise the round moves to the next channel.
- **Anchors are captured once per channel at round start**, via `anchorFor`.
- **A server whose channels are unknown** (persisted, or set manually) gets a unicast DISCOVER (hop limit 7) first. The sync starts when its ANNOUNCE arrives.
- **Cursor:**
  - A `FIRST` packet sets the cursor to its last item.
  - A later packet advances the cursor only if `items[0].seq == cursor + 1`. Otherwise it is a gap: stop advancing until the next request.
  - A `BATCH` epoch different from the state's epoch sets `state.epoch` and accepts the packet as `FIRST`.
- **Any `BATCH` from the server counts as answered**, including an empty one.

- [ ] **Step 1: Write the failing tests** (the same `ok()` style). Names and the key assertions:
  - `test_off_mode_sends_nothing`: `poll` → false for 2 h of simulated time.
  - `test_no_server_discovers_at_boot_and_hourly`:
    - At t=0, `poll` gives `Send{to=0xFFFFFFFF, chanIdx=-1, hopLimit=3}`, decoding as DISCOVER.
    - There is no further DISCOVER until t = 60 min, and there is one then.
  - `test_adopts_first_matching_announce`:
    - An ANNOUNCE from A listing no matching channel is ignored.
    - The next one, from B, lists a channel the node has → `serverId() == B`, `!manual()`, and `takeNotice() == NOTICE_FOUND`.
  - `test_manual_server_unreachable_notice_once_never_replaced`:
    - `setServer(M, true)`. ANNOUNCEs from others are ignored.
    - After 3 timeouts → `NOTICE_UNREACHABLE` once; a 4th timeout gives no new notice. `serverId()` is still M.
    - A later BATCH from M → `NOTICE_REACHABLE`.
  - `test_auto_boot_sync`:
    - The server is persisted, with channels unknown → unicast DISCOVER to it, hop limit 7.
    - ANNOUNCE (hops 2, channels 0 and 2) → REQUEST on channel 0 with hop limit 3, carrying the saved epoch, cursor and the anchor from `anchorFor(0)`.
    - After a `LAST` BATCH → REQUEST on channel 2.
  - `test_more_reasks_after_30s`: `LAST|MORE` → the same channel's REQUEST is not sent at 29.9 s, and is sent at 30 s.
  - `test_auto_resync_every_15_min`: after a round completes at t → the next round starts at t + 15 min, not before.
  - `test_manual_mode_no_auto_sync`:
    - Mode MANUAL with a known server → no REQUEST in 1 h.
    - `checkNow` → true, and REQUESTs follow.
    - A second `checkNow` within 5 min → false with why "cooldown"; at 5 min → true.
  - `test_gap_stops_cursor`:
    - Packets: FIRST with seqs 1–3; then 4–5; then 8–9.
    - The cursor ends at 5.
    - All 7 items are returned for posting.
    - The next REQUEST carries cursor 5.
  - `test_batch_items_returned_even_on_gap`: covered by the above; assert that `onBatch` returns `n` for the gap packet.
  - `test_new_epoch_resets_cursor`: the saved state is {epoch 7, cursor 40}; a BATCH with epoch 9 and seqs 1–2 → state {9, 2}.
  - `test_ignores_batch_from_other_node_or_unmatched_channel`: no state change, and it doesn't count as answered.
  - `test_set_server_resets_states`: after `setServer`, every `state(i)` is {0, 0} and `takeStatesDirty()` is true.
  - `test_display_epoch_from_age`:
    - `displayEpoch(1000, true, 100) == 900`.
    - Unknown age (`0xFFFFFFFF`) → 0.
    - Clock not set → 0.
- [ ] **Step 2:** `tests/run.sh`. Expected: `test_cs_client` BUILD FAILED (missing header).
- [ ] **Step 3: Implement** `src/cs_client.{h,cpp}`. No Arduino includes; `<functional>` is allowed.
- [ ] **Step 4:** `tests/run.sh`. Expected: all pass, with no warnings (`-Werror`).
- [ ] **Step 5: Commit** `feat: chat server client state machine`.

---

### Task 4: `ChannelMgr` — find, newest, and insert in time order

**Files:**
- Modify: `src/channel_mgr.h`, `src/channel_mgr.cpp`

**Interfaces:**
- Produces:
  - `bool hasMessage(int chanIdx, uint32_t fromNodeId, uint32_t packetId) const;`: any line with that sender and packet ID.
  - `bool newestReceived(int chanIdx, uint32_t myNodeId, uint32_t &fromNodeId, uint32_t &packetId) const;`: the newest line with `packetId != 0` and a `senderNodeId` that is neither 0 nor `myNodeId`.
  - `int insertMessageByEpoch(int chanIdx, const char *prefix, const char *text, uint16_t color, uint32_t packetId, uint32_t senderNodeId, uint32_t epoch);`
    - Word-wraps exactly like `addMessage`.
    - Inserts the wrapped lines before the first message-start line whose `epoch > epoch`. Appends if there is none, or if `epoch == 0`.
    - Shifts later lines in the PSRAM ring. When the ring is full, the oldest lines drop as `addMessage` does.
    - Adjusts the `lineIdx` of `_pending` entries at or after the insertion point.
    - Marks persistence dirty and sets `unread` like `addMessage`.

- [ ] **Step 1: Implement.** Reuse `_wordWrap` by wrapping into a temporary array first. Factor `_wordWrap`'s line generation from its `_pushLine` calls if needed. No behaviour change to `addMessage`.
- [ ] **Step 2: Build** `scripts/build-upload-monitor.sh -B -H` (Heltec V4 expansion env). Expected: SUCCESS.
- [ ] **Step 3: Commit** `feat: ChannelMgr find/newest/insert-by-time for replayed messages`. The behaviour is verified on the device in Task 8: `test_post_skips_known_message`, and ordering when a live message is newer.

---

### Task 5: config — repurpose the S&F fields, YAML, cursor storage

**Files:**
- Modify: `src/config_io.h`, `src/config_io.cpp`, `src/config.h`, `src/main_lvgl.cpp` (prefs migration at line ~10721)

**Interfaces:**
- Produces (in `RhinoConfig`):
  - `uint8_t chatServerMode;`: renamed in place from `bool snfClientEnabled`. Same size and offset. Old `true` (1) reads as `MODE_AUTO`.
  - `uint32_t chatServerNodeId;`: renamed in place from `snfRouterNodeId`.
  - `uint8_t chatServerFlags;`: takes the first byte of `_reservedPad16[2]` (leaving `_reservedPad16[1]`).
    - bit0 = manual, bit7 = migrated.
    - On load, if bit7 is clear: set `chatServerNodeId = 0` (an old S&F router pin is not a chat server) and set `flags = 0x80`.
  - Macros: `MY_CHAT_SERVER_MODE 1` replaces `MY_SNF_CLIENT_EN`. `MY_SNF_ROUTER_ID` and `SNF_HISTORY_WINDOW_MIN` are removed.
- YAML: replace the `module_config.storeForward` block with:

```yaml
  chatServer:
    mode: auto|manual|off
    server: !xxxxxxxx|none
    manual: true|false
```

  The importer still accepts and ignores `storeForward`.
- Cursor storage (in `main_lvgl.cpp`):
  - `static void chatServerLoadStates(csc::ChannelState out[MESH_CHANNELS]);`
  - `static void chatServerSaveStates(const csc::Client &c);`
  - Preferences namespace `"cschan"`, one blob `{uint32_t serverId; ChannelState s[MESH_CHANNELS];}`.
  - Discarded on load when `serverId != s_cfg.chatServerNodeId`.

- [ ] **Step 1: Make the changes.** Keep the comment style in `config_io.h`: every reused byte gets a note on why it is safe.
- [ ] **Step 2: Check the layout.** Add a `static_assert(offsetof(RhinoConfig, chatServerFlags) == <old offset of _reservedPad16>)` and confirm `sizeof(RhinoConfig)` is unchanged, in the same way the file's existing comments say layouts were verified.
- [ ] **Step 3: Build** `scripts/build-upload-monitor.sh -B -H`. Expected: SUCCESS.
- [ ] **Step 4: Commit** `feat: chat server config fields (replacing S&F), YAML, cursor storage`.

---

### Task 6: remove the Meshtastic Store & Forward client

**Files:**
- Modify: `src/main_lvgl.cpp`, `src/web_config.cpp`, `src/web_config.h`, `src/mesh_proto.h`, `src/mesh_proto.cpp`, `lang/*.lang`

- [ ] **Step 1: Delete from `main_lvgl.cpp`:**
  - `snf*` / `s_snf*` state and functions, `sendStoreForwardTo`, `serviceWebSnfRequest`.
  - The `CFG_ACTION_SNF_CLIENT` / `CFG_ACTION_SNF_REQUEST` enum entries, labels, enable rules, list entries and exec cases.
  - The `STORE_FORWARD_APP` handler body. Keep `case STORE_FORWARD_APP: appendLiveRxSummary(pkt, chanIdx, "F"); return false;` so the traffic stays visible in the Live feed.
- [ ] **Step 2: Delete from `web_config.cpp/.h`:**
  - The S&F settings block and its form handling.
  - The `/snf-request` route, its button and `webCfgTakeSnfRequest`/`webCfgSetSnfResult`.
- [ ] **Step 3: Delete from `mesh_proto`:** `encodeStoreForward` and `enum StoreForwardRR`. Keep `STORE_FORWARD_APP` and its `portnumName` entry.
- [ ] **Step 4: Remove now-stale S&F keys** from `lang/*.lang`. Run `python3 tools/i18n_audit.py`. Expected: no "bad", and no S&F keys listed as stale.
- [ ] **Step 5:** `grep -rniE "snf|store ?& ?f|storeforward|\[SF\]" src` → only the `STORE_FORWARD_APP` portnum lines remain. Run `scripts/build-upload-monitor.sh -B -H` → SUCCESS.
- [ ] **Step 6: Commit** `refactor: remove Meshtastic Store & Forward client (replaced by chat server)`.

---

### Task 7: glue — radio, posting, notices, time

**Files:**
- Modify: `src/main_lvgl.cpp` (where the S&F code was, ~line 50200), `src/mesh_proto.*` only if a helper is missing

**Interfaces:**
- Consumes: `csc::Client` (Task 3), the `ChannelMgr` additions (Task 4), the config and state storage (Task 5).
- Produces (static, in `main_lvgl.cpp`):

```cpp
static csc::Client s_csClient;
static void chatServerBegin();                       // after config + channels are loaded
static void chatServerService(uint32_t nowMs);       // from loop(), LOOP_PHASE("cs", ...)
static bool chatServerHandleRx(const MeshPacket &pkt);   // port 256 on a channel; true = consumed
static bool chatServerTryDiscovery(MeshPacket &pkt);     // undecrypted packet on the discovery hash
static bool chatServerCheckNow(const char **why);    // for the cfg row, web and node action
static void chatServerSetManual(uint32_t nodeId);    // node action "Use as Chat Server"
static void chatServerClear();
```

- **Discovery channel:**
  - Not added to `CHANNEL_KEYS`. In the undecrypted-packet path, if `pkt.hdr.channel == computeChannelHash("camillia-cs", DISCOVERY_KEY, 16)`, decrypt `pkt.rawCipher` with `encryptPayload(hdr.id, hdr.from, DISCOVERY_KEY, 16, cipher, plain, len)` (AES-CTR is symmetric). Then `decodeData`. If the portnum is 256, hand it to the client as ANNOUNCE.
  - Sending on the discovery channel uses the same key and hash.
- **Sending:** copy the `sendStoreForwardTo` frame pattern (encrypt with `CHANNEL_KEYS[chanIdx]`, `hdr.flags = hop | hop << 5`). The Data message is portnum 256, payload and bitfield 0. Log `T CS U <dst> <type> TX|ER` to the Live feed.
- **`anchorFor(i)`:** `Channels.newestReceived(i, s_myNodeId, from, id)`.
- **Posting BATCH items:**
  - For each item, unless `Channels.hasMessage(i, from, packetId)`, call `insertMessageByEpoch` with:
    - the prefix built like `appendRxText`'s;
    - `msgTransportIcon` replaced by a chat-server marker: `LV_SYMBOL_REFRESH`, or `"CS"` text where symbol fonts are absent (Cardputer path);
    - the time label from `displayEpoch` (or `--:--` when 0).
  - Then raise the channel alert once per batch, not per item, respecting mute.
- **Notices** (`takeNotice()` → `liveFeedAddPrefixed`):
  - FOUND → "Chat server XXXX found".
  - UNREACHABLE → "Can't reach chat server XXXX" (red).
  - REACHABLE → "Chat server XXXX reachable again".
- **Time:** if the BATCH has `FLAG_TIME_VALID`, and `!clockIsSet()` and `!timeSourceIsManual()` → `settimeofday(serverTime)`.
- **Persistence:** when `takeStatesDirty()`, save the states. When the server changes, write `s_cfg.chatServerNodeId/Flags` with `markConfigDirty()`.

- [ ] **Step 1: Implement and wire.** Call `chatServerBegin()` at the end of setup. Add `chatServerService` to the loop. Route port 256 to `chatServerHandleRx`, and undecrypted packets to `chatServerTryDiscovery`.
- [ ] **Step 2:** `tests/run.sh` (unchanged, all pass) and `scripts/build-upload-monitor.sh -B -H` → SUCCESS.
- [ ] **Step 3: Commit** `feat: chat server client glue (radio, posting, notices, time)`.

---

### Task 8: UI, then all-board build and end-to-end on hardware

**Files:**
- Modify: `src/main_lvgl.cpp`, `src/web_config.cpp`, `src/web_config.h`

**Interfaces:**
- Consumes: Task 7's functions.

- [ ] **Step 1: Device Config rows,** in the "Mesh modules" group where the S&F rows were:
  - `CFG_ACTION_CS_MODE`: label "Chat Server: Automatic|Manual only|Off"; activating cycles the mode.
  - `CFG_ACTION_CS_SERVER`: label "Chat Server Node: XXXX (manual)|XXXX|none"; activating clears it, with status "Chat server cleared". Disabled when none.
  - `CFG_ACTION_CS_CHECK`: "Check Chat Server Now". Its status is the `checkNow` outcome. Disabled when the mode is Off.
- [ ] **Step 2: Nodes action menu:** add "Use as Chat Server", which calls `chatServerSetManual(nodeId)`.
- [ ] **Step 3: Web config:**
  - A "Chat Server" block where S&F was:
    - mode select (`cs_mode`);
    - a server text field (`cs_server`, accepts `!xxxxxxxx` or a short name; blank = clear);
    - help text.
  - A "Check Chat Server Now" form under Utilities, posting to `/cs-check`. It uses the take/set-result pattern the removed `/snf-request` used.
  - A short name is resolved via `Nodes`. If ambiguous, the most recently heard node wins.
- [ ] **Step 4: Build every board:** `scripts/build-upload-monitor.sh -B`. Expected: the summary shows every env "ok". Run `tests/run.sh` → all pass. Run `python3 tools/i18n_audit.py` → no bad.
- [ ] **Step 5: End-to-end** with the Heltec server (`RiCs`) and one camillia-mt device. Ask the user which board to flash. Run `scripts/build-upload-monitor.sh -<board>`.
  1. With no server set, the Live feed shows "Chat server RiCs found" within a minute of boot. The Config row shows RiCs.
  2. Power the device off. Send 30+ messages on a shared channel from another node, then power it on. The messages arrive across several batches, in time order, with the chat-server marker. The server's serial log shows MORE and a re-request 30 s later.
  3. **`test_post_skips_known_message`:** send one more live message, then "Check Chat Server Now" → it is not duplicated.
  4. A live message newer than the replayed ones stays below them.
  5. Unplug the server. After 3 checks (use Check now, waiting for the cooldown, or set the mode to Automatic and wait), the Live feed shows "Can't reach chat server RiCs" once. The Config row still shows RiCs.
  6. Set "Use as Chat Server" on another node → the row reads "(manual)". A broadcast ANNOUNCE from RiCs doesn't replace it.
  7. Manual-only mode → no request at boot (server serial log is quiet). Check now works.
- [ ] **Step 6: Commit** `feat: chat server client UI (device, web) and end-to-end verified`.

---

## Self-review notes

- **Spec coverage:**
  - §6.1 → T2, T3, T7.
  - §6.2 → T5, T8.
  - §6.3: boot/15 min/manual/check now/discovery/anchor/no-answer → T3 tests, T7 notices.
  - §6.4: time order, dedupe, marker → T4, T7, T8.
  - S&F removal → T6.
  - §4.5 hops → T3.
  - Spec §8 end-to-end items 2, 3 and 6 → T8 step 5.
- **Server dependency:** T1 must land before T2 copies `cs_proto`.
