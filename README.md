# camillia-chat-server
Camillia chat server

## Hardware

| Board | PlatformIO env | Release image |
|---|---|---|
| Heltec WiFi LoRa 32 V4 (OLED) | `heltec-v4` | `camillia-chat-server-heltec-v4-<tag>.bin` |
| Heltec V4 + expansion board (TFT) | `heltec-v4-expansion` | `camillia-chat-server-heltec-v4-expansion-<tag>.bin` |
| Seeed Wio Tracker L2 | `wio-tracker-l2` | `camillia-chat-server-wio-tracker-l2-<tag>.bin` |

The expansion build uses the TFT with touch; the plain board uses its OLED.
The Wio Tracker L2 build uses its TFT with touch, and the Wake button acts as a tap.

Flash the factory image once over USB (`flash.sh`). After that, updates can
come over the air.

## Web config

Join the open `camillia-cs-<short name>` access point and browse to
`192.168.4.1`. Once a home WiFi network is set and joined, the access point is
turned off and the page is reached at the device's home-network IP, shown in
the display footer. If the home network stays down for 30 seconds, the access
point comes back.

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
