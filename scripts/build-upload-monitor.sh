#!/bin/bash
# Build, upload and monitor the camillia chat server — modelled on
# camillia-mt's scripts/build-upload-monitor.sh, for this repo's boards.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

ENV_NAME=""
ENV_EXPLICIT=false
# What a non-interactive run gets when no target was given.
DEFAULT_ENV_NAME="heltec-v4"
TEST_ENV_NAME="native"
ERASE_FIRST=false
FULLCLEAN=false
JUST_BUILD=false
RUN_TESTS=false
PORT=""

show_usage() {
	echo "Usage: $0 [--expansion|-X | --r8|-R | --wio|-W | --env|-e ENV] [--erase|-E] [--fullclean|-F] [--just-build|-B] [--test|-T] [--port|-p PORT]"
	echo "  --expansion, -X   Build the Heltec V4 + expansion board (TFT) env instead of the plain V4"
	echo "  --r8, -R          Build the Heltec V4-R8 + Expansion Kit V2 (TFT) env instead of the plain V4"
	echo "  --wio, -W         Build the Seeed Wio Tracker L2 env instead of the plain V4"
	echo "  --env, -e ENV     Build any other env, e.g. tdeck, tdeck-pro, tlora-pager-tft,"
	echo "                    mesh-deck, m9, crowpanel-35, crowpanel-50, p4-amoled-sx1262,"
	echo "                    p4-amoled-lr2021 (envs/*.ini)"
	echo "  --erase, -E       Erase the whole flash before uploading (settings and stored"
	echo "                    messages are wiped too)"
	echo "  --fullclean, -F   Run PlatformIO fullclean before building"
	echo "  --just-build, -B  Compile only - no upload, no monitor, no device needed"
	echo "  --test, -T        Run the native unit tests first; stop if any fail"
	echo "  --port, -p PORT   Serial port to use (default: PlatformIO auto-detect)"
	echo "With no target given you are asked to pick one; a non-interactive run uses $DEFAULT_ENV_NAME."
}

# Device envs offered by the menu, in order, as "env|label". -e still reaches
# the rest (heltec-v4-test, native).
DEVICE_ENVS=(
	"heltec-v4|Heltec V4 (OLED)"
	"heltec-v4-expansion|Heltec V4 Expansion Kit"
	"heltec-v4-r8|Heltec V4-R8 + Expansion Kit V2"
	"wio-tracker-l2|Seeed Wio Tracker L2"
	"tdeck|LilyGo T-Deck"
	"tdeck-pro|LilyGo T-Deck Pro"
	"tlora-pager-tft|LilyGo T-Lora Pager TFT"
	"mesh-deck|Attaky Mesh Deck"
	"m9|Elecrow ThinkNode M9"
	"crowpanel-35|Elecrow CrowPanel Advance 3.5"
	"crowpanel-50|Elecrow CrowPanel Advance 5.0"
	"p4-amoled-sx1262|LilyGo T-Display P4 AMOLED (SX1262)"
	"p4-amoled-lr2021|LilyGo T-Display P4 AMOLED (LR2021)"
)

# Envs live in platformio.ini and envs/*.ini.
has_env() {
	grep -qs "^\[env:$1\]" platformio.ini envs/*.ini
}

prompt_for_device() {
	local options=()
	local labels=()
	local entry
	for entry in "${DEVICE_ENVS[@]}"; do
		if has_env "${entry%%|*}"; then
			options+=("${entry%%|*}")
			labels+=("${entry#*|}")
		fi
	done

	if [ "${#options[@]}" -eq 0 ]; then
		echo "No supported device environments found in platformio.ini or envs/"
		exit 1
	fi

	if [ ! -t 0 ]; then
		ENV_NAME="$DEFAULT_ENV_NAME"
		echo "[PIO] Non-interactive shell detected, using device env: $ENV_NAME"
		return
	fi

	echo "Select device to build for:"
	local i
	for i in "${!options[@]}"; do
		printf "  %2d) %s (%s)\n" "$((i + 1))" "${labels[$i]}" "${options[$i]}"
	done

	local choice
	while true; do
		read -r -p "Enter choice [1-${#options[@]}]: " choice
		if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le "${#options[@]}" ]; then
			ENV_NAME="${options[$((choice - 1))]}"
			echo "[PIO] Selected device env: $ENV_NAME"
			return
		fi
		echo "Invalid selection. Please choose a number between 1 and ${#options[@]}."
	done
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

# The P4 envs need their own PlatformIO core: pioarduino's Arduino 3.x package
# shares a name with the S3 envs' 2.x one (as in camillia-mt).
P4_PIO_CORE_DIR="${CAMILLIA_P4_PIO_CORE_DIR:-${PLATFORMIO_CORE_DIR:-$HOME/.platformio}-p4}"
pio() {
	case "$ENV_NAME" in
		p4-amoled-*) PLATFORMIO_CORE_DIR="$P4_PIO_CORE_DIR" command pio "$@" ;;
		*)           command pio "$@" ;;
	esac
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
		--expansion|-X)  ENV_NAME="heltec-v4-expansion"; ENV_EXPLICIT=true ;;
		--r8|-R)         ENV_NAME="heltec-v4-r8"; ENV_EXPLICIT=true ;;
		--wio|-W)        ENV_NAME="wio-tracker-l2"; ENV_EXPLICIT=true ;;
		--env|-e)
			if [ $# -lt 2 ]; then echo "--env needs a value"; exit 1; fi
			ENV_NAME="$2"
			ENV_EXPLICIT=true
			shift
			;;
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

if [ "$ENV_EXPLICIT" = true ]; then
	if ! has_env "$ENV_NAME"; then
		echo "Environment '$ENV_NAME' not found in platformio.ini or envs/*.ini"
		exit 1
	fi
else
	prompt_for_device
fi

if [ "$JUST_BUILD" = true ] && [ "$ERASE_FIRST" = true ]; then
	echo "--erase needs a connected device; it cannot be combined with --just-build."
	exit 1
fi

if [ "$RUN_TESTS" = true ]; then
	echo "[PIO] Native unit tests..."
	command pio test -e "$TEST_ENV_NAME"
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
