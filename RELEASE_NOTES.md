### Flashing
Download `flash.sh` and the image for your board, then run `./flash.sh <image> [port]`. The port defaults to `/dev/ttyUSB0`; on macOS it's usually `/dev/cu.usbmodem*`.

| Board | Image |
|---|---|
| Heltec WiFi LoRa 32 V4 (OLED) | `camillia-chat-server-heltec-v4-<tag>.bin` |
| Heltec V4 + expansion board (TFT) | `camillia-chat-server-heltec-v4-expansion-<tag>.bin` |

A plain flash keeps settings and stored messages. `--erase` wipes both.
