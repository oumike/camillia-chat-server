#!/bin/bash
# Build, upload and monitor the camillia chat server — modelled on
# camillia-mt's scripts/build-upload-monitor.sh, for this repo's boards.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

ENV_NAME="heltec-v4"
TEST_ENV_NAME="native"
ERASE_FIRST=false
FULLCLEAN=false
JUST_BUILD=false
RUN_TESTS=false
PORT=""

show_usage() {
	echo "Usage: $0 [--expansion|-X] [--erase|-E] [--fullclean|-F] [--just-build|-B] [--test|-T] [--port|-p PORT]"
	echo "  --expansion, -X   Build the Heltec V4 + expansion board (TFT) env instead of the plain V4"
	echo "  --erase, -E       Erase the whole flash before uploading (settings and stored"
	echo "                    messages are wiped too)"
	echo "  --fullclean, -F   Run PlatformIO fullclean before building"
	echo "  --just-build, -B  Compile only - no upload, no monitor, no device needed"
	echo "  --test, -T        Run the native unit tests first; stop if any fail"
	echo "  --port, -p PORT   Serial port to use (default: PlatformIO auto-detect)"
	echo "Builds the '$ENV_NAME' environment (default heltec-v4, or heltec-v4-expansion with -X)."
}

format_duration() {
	local total_seconds="$1"
	local hours=$((total_seconds / 3600))
	local minutes=$(((total_seconds % 3600) / 60))
	local seconds=$((total_seconds % 60))

	if [ "$hours" -gt 0 ]; then
		printf "%dh %02dm %02ds" "$hours" "$minutes" "$seconds"
	else
		printf "%dm %02ds" "$minutes" "$seconds"
	fi
}

run_pio_target() {
	local target="$1"
	local label="$2"
	echo "[PIO] $label ($ENV_NAME)..."
	pio run -e "$ENV_NAME" -t "$target" ${PORT:+--upload-port "$PORT"}
}

if ! command -v pio >/dev/null 2>&1; then
	echo "PlatformIO CLI not found: install it or run from an environment that provides 'pio'."
	exit 1
fi

while [ $# -gt 0 ]; do
	case "$1" in
		--expansion|-X)  ENV_NAME="heltec-v4-expansion" ;;
		--erase|-E)      ERASE_FIRST=true ;;
		--fullclean|-F)  FULLCLEAN=true ;;
		--just-build|-B) JUST_BUILD=true ;;
		--test|-T)       RUN_TESTS=true ;;
		--port|-p)
			if [ $# -lt 2 ]; then echo "--port needs a value"; exit 1; fi
			PORT="$2"
			shift
			;;
		--help|-h)       show_usage; exit 0 ;;
		*)
			echo "Unknown argument: $1"
			show_usage
			exit 1
			;;
	esac
	shift
done

if [ "$JUST_BUILD" = true ] && [ "$ERASE_FIRST" = true ]; then
	echo "--erase needs a connected device; it cannot be combined with --just-build."
	exit 1
fi

if [ "$RUN_TESTS" = true ]; then
	echo "[PIO] Native unit tests..."
	pio test -e "$TEST_ENV_NAME"
fi

BUILD_START_TS="$(date +%s)"
if [ "$FULLCLEAN" = true ]; then
	echo "[PIO] Full clean ($ENV_NAME)..."
	pio run -e "$ENV_NAME" -t fullclean
fi

if [ "$JUST_BUILD" = true ]; then
	echo "[PIO] Build ($ENV_NAME)..."
	pio run -e "$ENV_NAME"
	BUILD_END_TS="$(date +%s)"
	BIN_PATH=".pio/build/${ENV_NAME}/firmware.bin"
	SIZE_NOTE=""
	[ -f "$BIN_PATH" ] && SIZE_NOTE=", $(( $(wc -c <"$BIN_PATH") / 1024 )) KB"
	echo "[PIO] Build completed in $(format_duration $((BUILD_END_TS - BUILD_START_TS)))${SIZE_NOTE}."
	exit 0
fi

if [ "$ERASE_FIRST" = true ]; then
	run_pio_target "erase" "Erasing device flash"
fi

run_pio_target "upload" "Upload"
BUILD_END_TS="$(date +%s)"
echo "[PIO] Build completed in $(format_duration $((BUILD_END_TS - BUILD_START_TS)))."

ELF_PATH=".pio/build/${ENV_NAME}/firmware.elf"
BIN_PATH=".pio/build/${ENV_NAME}/firmware.bin"
if [ -f "$ELF_PATH" ]; then
	ELF_SHA="$(shasum -a 256 "$ELF_PATH" | awk '{print $1}')"
	echo "[PIO] ELF SHA256: $ELF_SHA"
fi
if [ -f "$BIN_PATH" ]; then
	BIN_SHA="$(shasum -a 256 "$BIN_PATH" | awk '{print $1}')"
	echo "[PIO] BIN SHA256: $BIN_SHA"
fi

if [ ! -t 0 ]; then
	echo "[PIO] Not an interactive terminal; skipping the serial monitor."
	exit 0
fi
echo "[PIO] Monitor ($ENV_NAME)..."
pio device monitor -e "$ENV_NAME" ${PORT:+--port "$PORT"}
