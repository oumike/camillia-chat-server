#!/bin/bash
# Flash the camillia chat server to a connected Heltec WiFi LoRa 32 V4.
# Expects the merged factory image (bootloader + partitions + boot_app0 + app)
# published by the release workflow: camillia-chat-server-heltec-v4-vX.Y.Z.bin.
# That layout is written at 0x0; an app-only firmware.bin from .pio/build/ is
# NOT compatible with this script.
#
# Usage: ./flash.sh [--erase|-E] <firmware.bin> [port]
#   --erase, -E   Erase the whole chip before writing.
# Default port: /dev/ttyUSB0 (Linux) — use /dev/cu.usbmodem* on macOS.
#
# Erase is opt-in: a plain flash leaves NVS (settings, WiFi, channels) and the
# LittleFS message store alone, so an update keeps everything the server holds.

set -e

ERASE=0
ARGS=()
for arg in "$@"; do
    case "$arg" in
        --erase|-E) ERASE=1 ;;
        *)          ARGS+=("$arg") ;;
    esac
done

FIRMWARE=${ARGS[0]:-}
PORT=${ARGS[1]:-/dev/ttyUSB0}

if [[ -z "$FIRMWARE" ]]; then
    FIRMWARE=$(ls camillia-chat-server-*.bin 2>/dev/null | sort -V | tail -1)
fi

if [[ -z "$FIRMWARE" || ! -f "$FIRMWARE" ]]; then
    echo "Usage: ./flash.sh [--erase|-E] <camillia-chat-server-heltec-v4-vX.Y.Z.bin> [port]"
    exit 1
fi

if [[ "$ERASE" == "1" ]]; then
    echo "Erasing $PORT (settings and stored messages will be lost)..."
    esptool.py --chip esp32s3 --port "$PORT" --baud 921600 \
        --before default_reset --after no_reset \
        erase_flash
fi

echo "Flashing $FIRMWARE to $PORT..."
esptool.py --chip esp32s3 --port "$PORT" --baud 921600 \
    --before default_reset --after hard_reset \
    write_flash -z 0x0 "$FIRMWARE"
