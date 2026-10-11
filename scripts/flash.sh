#!/bin/bash
# Flash the camillia chat server to a connected board. Each board has its own
# image, camillia-chat-server-<board>-vX.Y.Z.bin; the boards are listed below.
# The chip (ESP32-S3, or ESP32-P4 for the T-Display P4) is detected.
# Expects the merged factory image (bootloader + partitions + boot_app0 + app)
# published by the release workflow.
# That layout is written at 0x0; an app-only firmware.bin from .pio/build/ is
# NOT compatible with this script.
#
# Usage: ./flash.sh [--erase|-E] <firmware.bin> [port]
#   --erase, -E   Erase the whole chip before writing.
# With no file given, the newest camillia-chat-server-*.bin in the current
# directory is used, unless the directory holds more than one board's images;
# then they are listed and you pick one.
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
    CANDIDATES=$(ls camillia-chat-server-*.bin 2>/dev/null | sort -V || true)
    # Board = text between "camillia-chat-server-" and the trailing -vX.Y.Z[-alpha.N].bin
    # ("heltec-v4" itself contains "-v4", so anchor on the full version suffix).
    BOARDS=$(echo "$CANDIDATES" | sed -nE 's/^camillia-chat-server-(.+)-v[0-9]+\.[0-9]+\.[0-9]+(-alpha\.[0-9]+)?\.bin$/\1/p' | sort -u)
    if [[ $(echo "$BOARDS" | grep -c .) -gt 1 ]]; then
        echo "More than one board's firmware found:"
        echo "$CANDIDATES" | sed 's/^/  /'
        echo "Pick one: ./flash.sh <file>"
        exit 1
    fi
    FIRMWARE=$(echo "$CANDIDATES" | tail -1)
fi

if [[ -z "$FIRMWARE" || ! -f "$FIRMWARE" ]]; then
    echo "Usage: ./flash.sh [--erase|-E] <camillia-chat-server-BOARD-vX.Y.Z.bin> [port]"
    echo "  BOARD: heltec-v4, heltec-v4-expansion, heltec-v4-r8, wio-tracker-l2, tdeck,"
    echo "         tdeck-pro, tlora-pager-tft, mesh-deck, m9, crowpanel-35, crowpanel-50,"
    echo "         p4-amoled-sx1262 or p4-amoled-lr2021"
    exit 1
fi

if [[ "$ERASE" == "1" ]]; then
    echo "Erasing $PORT (settings and stored messages will be lost)..."
    esptool.py --chip auto --port "$PORT" --baud 921600 \
        --before default_reset --after no_reset \
        erase_flash
fi

echo "Flashing $FIRMWARE to $PORT..."
esptool.py --chip auto --port "$PORT" --baud 921600 \
    --before default_reset --after hard_reset \
    write_flash -z 0x0 "$FIRMWARE"
