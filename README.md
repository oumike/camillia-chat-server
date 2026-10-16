# camillia-chat-server
Camillia chat server

## Hardware

| Board | PlatformIO env | Release image |
|---|---|---|
| Heltec WiFi LoRa 32 V4 (OLED) | `heltec-v4` | `camillia-chat-server-heltec-v4-<tag>.bin` |
| Heltec V4 + expansion board (TFT) | `heltec-v4-expansion` | `camillia-chat-server-heltec-v4-expansion-<tag>.bin` |
| Heltec V4-R8 + Expansion Kit V2 (TFT) | `heltec-v4-r8` | `camillia-chat-server-heltec-v4-r8-<tag>.bin` |
| Seeed Wio Tracker L2 | `wio-tracker-l2` | `camillia-chat-server-wio-tracker-l2-<tag>.bin` |
| LilyGo T-Deck | `tdeck` | `camillia-chat-server-tdeck-<tag>.bin` |
| LilyGo T-Deck Pro (e-paper) | `tdeck-pro` | `camillia-chat-server-tdeck-pro-<tag>.bin` |
| LilyGo T-LoRa Pager | `tlora-pager-tft` | `camillia-chat-server-tlora-pager-tft-<tag>.bin` |
| Attaky Mesh Deck | `mesh-deck` | `camillia-chat-server-mesh-deck-<tag>.bin` |
| Elecrow ThinkNode M9 | `m9` | `camillia-chat-server-m9-<tag>.bin` |
| Elecrow CrowPanel Advance 3.5" | `crowpanel-35` | `camillia-chat-server-crowpanel-35-<tag>.bin` |
| Elecrow CrowPanel Advance 5.0" | `crowpanel-50` | `camillia-chat-server-crowpanel-50-<tag>.bin` |
| LilyGo T-Display P4 + SX1262 | `p4-amoled-sx1262` | `camillia-chat-server-p4-amoled-sx1262-<tag>.bin` |
| LilyGo T-Display P4 + LR2021 | `p4-amoled-lr2021` | `camillia-chat-server-p4-amoled-lr2021-<tag>.bin` |

The expansion and V4-R8 builds use the TFT with touch; the plain board uses its
OLED. The V4-R8 build is untested on hardware.
The Wio Tracker L2 build uses its TFT with touch, and the Wake button acts as a tap.

The boards from the T-Deck down are camillia-mt's (every one but the
Cardputer, which has no PSRAM). Their envs live in `envs/*.ini` and their code
in `src/boards/<board>/`, with pins and drivers taken from camillia-mt. The
status pages scale with the screen; the T-Deck Pro shows one plain status page
on its e-paper, redrawn at most once a minute. A button stands in for a tap on
boards without touch: the trackball on the T-Deck, the wheel on the Pager,
BOOT on the M9 and P4. The CrowPanels have no battery sensing. All are
untested on hardware.

The P4 envs build with their own PlatformIO core, as in camillia-mt:
`PLATFORMIO_CORE_DIR=~/.platformio-p4 pio run -e p4-amoled-sx1262`
(`scripts/build-upload-monitor.sh -e p4-amoled-sx1262` does this for you).
The P4's ESP32-C6 needs ESP-Hosted firmware for WiFi; camillia-mt's
`flash-tdisplay-p4-c6.sh` installs the matching version.

Flash the factory image once over USB (`flash.sh`). After that, updates can
come over the air.

## DM commands

Direct-message the server on any of its channels and it replies to you:

- `help` — list the commands
- `status` — uptime, battery, MQTT, airtime and stored messages per channel

The server has its own Curve25519 key pair, made on first boot and kept in NVS,
and advertises the public key in its NODEINFO. DMs encrypted to that key (PKI,
which camillia-mt requires) are answered the same way; channel-key DMs, as
older Meshtastic clients send, are answered on their channel. A node that has
no key for the server yet gets it by asking: camillia-mt does this when the
first DM fails, and the next one goes through.

## Web config

Join the open `camillia-cs-<short name>` access point and browse to
`192.168.4.1`. Once a home WiFi network is set and joined, the access point is
turned off and the page is reached at the device's home-network IP, shown in
the display footer. If the home network stays down for 30 seconds, the access
point comes back.

**Send announcement** in the page header, left of the IP address, broadcasts an ANNOUNCE on the
discovery channel, up to the configured max hops, so camillia nodes that are
searching pick up this server without waiting for their next DISCOVER.

## OTA updates

The **Firmware** card in the web config checks for and installs new stable
releases, from `http://ota.camillia.sumat.org/cs-firmware` (a proxy for this
repo's GitHub releases). The device checks 20 seconds after joining WiFi and
then once a day. With **Install updates automatically** on (`ota.autoInstall`
in the YAML backup), it installs a newer release itself and restarts.

Each release publishes `camillia-chat-server-<env>-<tag>-ota.bin` and a
detached ECDSA P-256 signature (`.sig`). The device checks that signature
against the public key in `src/ota_signing_pubkey.h` (the camillia-mt key)
before it switches to the new image, so plain HTTP is safe. Releases are signed
with `ota_signing_key.pem` (git-ignored), or with the `OTA_SIGNING_KEY` secret
of the `release` environment in CI.
