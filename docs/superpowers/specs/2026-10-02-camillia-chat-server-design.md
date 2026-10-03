# Camillia Chat Server — Design

**Date:** 2026-10-02
**Status:** Draft for review
**Repos affected:** `camillia-chat-server` (new firmware), `camillia-mt` (new client code)

> **Gate:** no changes are made to `camillia-mt` until the owner explicitly approves starting that work. Section 6 describes the intended client so the protocol can be designed against it; it is not authorization to edit camillia-mt.

## 1. Purpose

A camillia-mt node that was switched off or out of range misses channel messages. The camillia chat server is a small, always-on LoRa device that listens to a fixed set of Meshtastic channels, keeps a recent history of each, and sends a returning camillia-mt node whatever it missed.

It replaces the role of Meshtastic's Store & Forward, which we are not using. Throughout the code and UI it is called **camillia chat server**, never "store and forward".

**Success looks like:** a camillia-mt node boots after being away, and within a few minutes its channels contain the messages it missed, marked as delivered by the chat server — without the catch-up flooding the mesh.

## 2. Scope

**In scope (v1)**
- Server firmware for the Heltec WiFi LoRa 32 V4 (ESP32-S3, 2 MB PSRAM, 16 MB flash, built-in OLED), PlatformIO.
- Listening on LoRa and, optionally, MQTT (receive only).
- A new camillia chat server protocol on Meshtastic `PRIVATE_APP` (port 256).
- Server web config.
- camillia-mt client: discovery, requests, receiving and posting replays, settings, live-screen notice.

**Out of scope (v1)**
- Authentication beyond holding the channel key (planned later).
- Direct messages; only channel text messages are stored.
- Talking to clients over MQTT. The server never publishes to MQTT.
- A separate "fast lane" frequency for bulk transfer.
- The final design of the "received from chat server" visual cue (a basic marker ships in v1; refined later).

## 3. Overview

```
   LoRa (mesh preset) ──┐
                        ├─► decode + decrypt ─► dedupe ─► channel store (PSRAM, persisted to flash)
   MQTT (optional,      ┘     (monitored                        │
   subscribe only)             channels only)                   ▼
                                                     protocol handler (port 256)
                                                                │  batches, paced
                                                                ▼
                                                   LoRa replies to camillia-mt nodes
```

The server is on the same region, preset and frequency slot as the mesh it serves. It has one radio, and staying on the mesh frequency keeps it listening, lets stock Meshtastic nodes relay its replies, and keeps the asking node on the mesh too.

## 4. Protocol

### 4.1 Transport

- Carried in ordinary Meshtastic packets, `Data.portnum = 256` (`PRIVATE_APP`). Stock Meshtastic nodes relay these and otherwise ignore them.
- Every payload starts with two bytes: **protocol version** (`1`) and **message type**. Receivers drop unknown versions and types.
- All integers are little-endian. A whole payload must fit in Meshtastic's 233-byte `Data.payload` limit.

### 4.2 Channels and keys

| Message | Sent on | Why |
|---|---|---|
| `DISCOVER`, `ANNOUNCE` | The **discovery channel**: fixed name `camillia-cs` and a fixed 16-byte AES key compiled into both firmwares (`45a51ee6e782493ffb9cdb164bd7cdf7`, defined in `src/cs_proto.cpp`) | Any camillia node can find any server without knowing its channels |
| `REQUEST`, `BATCH` | The **real channel** being synced, encrypted with that channel's key | Only a node holding a channel's key can ask for, or read, its history. This is the only access control in v1 |

A node's channel and a server's channel **match** when both name and key are identical. Matching uses a **channel ID**: the first 4 bytes of `SHA-256(name || key)`, read little-endian. A 1-byte PSK is expanded to Meshtastic's 16-byte default-key form first, so `AQ==` and its expanded key give the same ID. Name alone is not enough, since many meshes have a "LongFast".

### 4.3 Messages

**`0x01 DISCOVER`** — node → broadcast, discovery channel. Empty body.

**`0x02 ANNOUNCE`** — server → the node that sent `DISCOVER`, discovery channel.

| Field | Size |
|---|---|
| short name length, short name | 1 + ≤4 |
| channel count `n` (1–10) | 1 |
| `n` × { channel ID, name length, name } | 4 + 1 + ≤11 each |

**`0x03 REQUEST`** — node → server (unicast), on the channel being synced. One request per channel.

| Field | Size | Notes |
|---|---|---|
| store epoch | 4 | Epoch from the last `BATCH` the node got from this server, or 0 |
| cursor | 4 | Highest contiguous sequence number received last time, or 0 |
| anchor sender | 4 | Sender of the newest message the node already had, or 0 |
| anchor packet ID | 4 | That message's packet ID, or 0 |

**`0x04 BATCH`** — server → node (unicast), on the channel being synced. A batch is one or more packets.

| Field | Size | Notes |
|---|---|---|
| store epoch | 4 | Random ID created when the server's store for this channel is created |
| flags | 1 | bit0 `LAST` (last packet of this batch), bit1 `MORE` (more messages after this batch), bit2 `TIME_VALID` |
| server time | 4 | Unix time; only meaningful if `TIME_VALID` |
| item count | 1 | |
| items | | Each: sequence (4), sender (4), packet ID (4), age in seconds (4; `0xFFFFFFFF` = unknown), text length (1), text (≤200) |

Several short messages are packed into one packet when they fit; a 200-byte message goes alone.

### 4.4 Where "since" comes from — no clocks involved

Clocks are not trusted: a node can have no time, or a wrong time it can't detect. Instead, the server picks the start point in this order:

1. **Cursor.** If the request's store epoch matches the server's, send everything after `cursor`. This is the normal case after the first sync.
2. **Anchor.** Otherwise, if the server holds the anchor message (sender + packet ID), send everything after it.
3. **Everything.** Otherwise, send the whole store for that channel.

Every server message has a **sequence number**, increasing per channel and saved with the store. If the server's store is wiped, it gets a new epoch, so stale cursors fall through to the anchor or "everything".

The node drops any message it already has (same sender and packet ID), so overlap is harmless.

**Message times.** The server sends each message's **age** ("heard 42 minutes ago"), and the node computes a display time from its own clock. If the server has a real clock (NTP over WiFi), it also sends `server time`, which a node with an unset clock may adopt. Messages stored before a server reboot have an unknown age if the server had no real clock; the node shows them without a time.

### 4.5 Exchanges

**Discovery** (node has no server):
1. Node broadcasts `DISCOVER` on the discovery channel, hop limit 3, once an hour.
2. Each server within its configured max hops answers with `ANNOUNCE`.
3. The node takes the **first** `ANNOUNCE` that lists at least one of its channels, stores that server's node ID (shown to the user by short name), and stops searching.

**Manually set server.** A server the user sets manually is **never overwritten**: not by discovery, not by another server's `ANNOUNCE`, and not after failed requests. Only the user's **Clear** or **Set manually** changes it. To learn its channels and distance, the node sends that server a unicast `DISCOVER` (hop limit 7) and uses the `ANNOUNCE` reply, without reconsidering which server it uses. Discovery broadcasts only happen while no server is set.

**Sync** (node has a server):
1. For each of its channels that the server's `ANNOUNCE` listed, the node sends a `REQUEST`.
2. The server queues it and, when its turn comes, sends up to *batch size* messages as `BATCH` packets, spaced by *packet gap*.
3. If the last packet has `MORE`, the node sends a new `REQUEST` with its updated cursor **30 seconds** later. This does not count against the 15-minute limit.
4. If packets were lost, the node's cursor is the highest *contiguous* sequence it got, so the next request re-sends the gap.

**Hops.** The server replies with a hop limit equal to the hops the request actually travelled (`hop_start − hop_limit`), capped at its configured **max hops** (default 7). It ignores requests and discovery from further away. The node sends requests with hop limit = hops to the server (learned from `ANNOUNCE`) + 1, capped at 7; for a manually set server it uses 7 until the first reply tells it the distance.

**Server load.** The server serves one request at a time from a FIFO queue of up to 24. A full queue drops the request; the node simply tries again on its next scheduled check.

## 5. Server firmware (`camillia-chat-server`)

### 5.1 Code layout

```
platformio.ini            Heltec V4 env, matching camillia-mt's (espressif32@7.0.1, Arduino,
                          16 MB flash, partitions_16mb_fs.csv)
lib/camillia_mesh/        Borrowed from camillia-mt (see 5.2)
src/
  main.cpp                Setup and main loop wiring
  config.{h,cpp}          Settings, stored in NVS
  channel_store.{h,cpp}   Per-channel ring buffers, dedupe, sequence numbers, persistence
  ingest_lora.{h,cpp}     Radio receive → decode → store
  ingest_mqtt.{h,cpp}     MQTT subscribe → decode → store
  cs_proto.{h,cpp}        Encode/decode of the section-4 messages (pure, no hardware)
  cs_server.{h,cpp}       Discovery replies, request queue, batching and pacing
  node_identity.{h,cpp}   Node ID, names, periodic NODEINFO
  web_ui.{h,cpp}          Web config
  status_display.{h,cpp}  OLED status page
test/                     Native unit tests (see section 8)
```

### 5.2 What is borrowed from camillia-mt

Only the protocol layer, copied into `lib/camillia_mesh/` and trimmed. **Not** the rest of the firmware.

| From camillia-mt | Used for |
|---|---|
| `mesh_radio` | Driving the V4's SX1262 radio |
| `mesh_proto` | Meshtastic packet encode/decode, channel encryption, `hop_start` |
| `xeddsa` | Only because `mesh_proto` depends on it |
| `mqtt_bridge` (decode half) | Decoding Meshtastic `ServiceEnvelope` messages from MQTT |
| region / preset tables (`mesh_channel_plan.h`, preset derivation from `config_io`) | Frequency and modem settings |

Dependencies on camillia-mt's `config.h` and `debug_flags.h` are replaced with small local shims. `lib/camillia_mesh/VENDORED.md` records the source commit so fixes can be carried over by hand.

### 5.3 Channel store

- Up to **10 channels**, **250 messages each** (compile-time constants for now). A ring is allocated only for configured channels.
- Each record about 224 bytes (sequence, sender, packet ID, receive time, text, source LoRa/MQTT): about 55 KB of PSRAM per channel, 550 KB at 10 channels (of 2 MB).
- When a channel is full, the oldest message is dropped.
- **Dedupe:** a message heard on both LoRa and MQTT, or relayed several times, is stored once (key: sender + packet ID).
- **Stored:** text messages (`TEXT_MESSAGE_APP`) broadcast on a monitored channel. **Not stored:** DMs, other ports, and port-256 traffic.
- **Persistence:** each channel is saved to LittleFS on the flash partition. It is saved when it has unsaved changes and either 60 seconds have passed or 20 new messages have arrived, which limits flash wear. It is loaded at boot.
- If a channel's name or key is changed in config, that channel's store is cleared and gets a new epoch.

### 5.4 Node identity

The server has a normal Meshtastic node ID and short/long name, set in web config. It broadcasts `NODEINFO` at boot and every 3 hours on its monitored channels, so nodes list it by short name and the user can pick it manually.

### 5.5 Web config

Reached through the server's own WiFi access point, and optionally through a home WiFi network (STA) when configured.

Two tabs: **Config** (the settings below, status, and backup) and **Messages** (stored messages for one monitored channel, picked from a drop-down, newest first, showing time, sender, source LoRa/MQTT and text; refreshes every 15 s while open). The Messages tab can also **clear** the selected channel or all channels, after a confirmation; a cleared channel gets a new epoch and is saved immediately, and any queued or in-progress replies for it are dropped.

| Section | Settings | Default |
|---|---|---|
| Identity | Short name, long name | Derived from MAC |
| Radio | Region, preset, frequency slot | US, LongFast, default slot |
| Channels | Number of channels (1–10); for each: name and key. The form shows at least 3 channel rows | 1 channel |
| Replies | Batch size | 10 messages |
| | Gap between packets | 3 s |
| | Max hops | 7 |
| MQTT | Enabled, broker, port, username, password, topic root | Off |
| WiFi | Optional STA SSID/password (also used for NTP). The config AP `camillia-cs-<short name>` is always open (no password) | AP only |
| Status | Per-channel message counts, last message heard, MQTT state, queue length | — |
| Backup | **Export** the whole config as a YAML file; **Import** a YAML file (upload or paste). Import validates first, then saves and restarts; keys it doesn't mention keep their values. The file includes channel keys and passwords | — |

With the defaults, clearing a full 250-message channel takes about 25 rounds of about 70 seconds each. In regions with a duty-cycle limit (for example EU 868 at 10%), the server must throttle its replies to stay within the limit, even if that is slower than the configured pacing.

### 5.6 OLED status

One plain, readable status page (layout may change later):

```
Camillia Chat Server
<node name>            (long name, configurable in web config)
<IP address>
MQTT: <off | no wifi | connecting | connected>
Stored: <number of messages stored, all channels>
```

The time of the last reply sent to a node is shown on the web status instead (`lastSent`).

### 5.7 MQTT ingest

Off by default. When on, the server subscribes under the configured topic root for its monitored channels, decodes and decrypts each `ServiceEnvelope` with `mesh_proto`, and stores the text messages exactly like LoRa ones. It never publishes. If the broker is unreachable, it retries with backoff and keeps working on LoRa.

## 6. camillia-mt client

### 6.1 New code

`chat_server_client.{h,cpp}`: discovery, request scheduling, cursor tracking, receiving and posting `BATCH` messages. The `cs_proto` encoder/decoder lives in this repo as the source of truth; camillia-mt vendors a copy, recorded the same way as `lib/camillia_mesh/VENDORED.md`.

**Decided:** camillia-mt's existing Meshtastic Store & Forward client is **removed entirely**: the port-65 handling, the **Request Replay** button, the router ID setting, heartbeat discovery, and the `[SF]` replay prefix. The camillia chat server replaces it. (Timing is still subject to the gate at the top of this spec.)

### 6.2 Settings (device config screen and web config)

| Setting | Values | Default |
|---|---|---|
| Chat server mode | Automatic / Manual only / Off | Automatic |
| Chat server | Short name of the current server; **Clear**; **Set manually** (pick by short name from known nodes). A manually set server is never replaced automatically | Empty |

Stored per server: node ID, distance in hops, and for each channel the store epoch and cursor (in NVS, so they survive reboots).

### 6.3 Behaviour

- **Automatic:** sync once on boot (once a server is known), then every **15 minutes**.
- **Manual only:** never sync on its own, including at boot. The user runs **Check now**.
- **Check now** is available in both modes, with a **5-minute** cooldown.
- **No server:** if the mode isn't Off, broadcast `DISCOVER` once an hour until a server is found.
- **Anchor:** for a channel with no cursor yet, the anchor is the newest message the node had on that channel **when the sync started**. Live messages arriving during a catch-up don't move it.
- **No answer:** a request with no `BATCH` within 60 seconds counts as unanswered. After **3 in a row**, show "Can't reach chat server ABCD" on the live screen. The server is **never** forgotten automatically; the node keeps trying on schedule. The notice clears on the next reply.

### 6.4 Posting replayed messages

- Messages are inserted into the channel's history in time order (using the age), not appended at the bottom, because the node may already have newer live messages. Messages with an unknown age are placed by sequence number, before the first replayed message that has a known age.
- Duplicates (same sender and packet ID) are skipped.
- Each replayed message carries a "from chat server" marker in the UI. v1 uses a simple marker; the final look is to be refined later.

## 7. Error handling summary

| Situation | Behaviour |
|---|---|
| Packet lost in a batch | Next request's cursor stops at the gap, so it is re-sent |
| Server store wiped | New epoch; node falls back to anchor, then "everything" |
| Node has no clock | Ages still work relative to receipt; `server time` may set the clock |
| Two servers in range | Node keeps the first that answered with a matching channel; a manually set server always wins |
| Request from too far away | Server ignores it |
| Server queue full | Request dropped; node retries on its next check |
| MQTT broker down | Retry with backoff; LoRa unaffected |
| Unknown protocol version | Dropped by both sides |

## 8. Testing

- **Native unit tests** (PlatformIO `native` env, no hardware) for:
  - `cs_proto` encode/decode round trips and size limits
  - the channel store: ring wraparound, dedupe, sequence numbers, epoch changes, persistence format
  - the "since" logic (cursor → anchor → everything) and batch packing
- **Hardware bring-up** on a Heltec V4 server plus one camillia-mt node:
  1. The server stores live LoRa messages and shows counts on the OLED and web page.
  2. Discovery: the node finds the server, and lists it by short name.
  3. Catch-up: with the node off, send 30+ messages; on boot it receives all of them across several batches, in order, marked.
  4. Duplicates: the same message heard over LoRa and MQTT is stored once.
  5. Reboot the server mid-history; the store survives and cursors still work.
  6. Unplug the server; after 3 failed checks the node shows the live-screen notice and keeps the server.

## 9. Delivery order

1. Server: borrowed mesh layer builds for the V4; the store fills from LoRa; OLED and web config working.
2. Protocol (`cs_proto`) with native tests.
3. Server: discovery and sync.
4. camillia-mt client — **only after the owner approves starting camillia-mt work.**
5. MQTT ingest.
