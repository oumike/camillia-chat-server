#!/bin/bash
# Release the camillia chat server — modelled on camillia-mt's scripts/release.sh,
# for this repo's single board and without OTA.
#
# By default this script builds nothing: it works out the version, writes and
# reviews RELEASE_NOTES.md, commits, pushes and dispatches
# .github/workflows/release.yml, which builds and publishes on GitHub.
# --build-local does all of it here instead.
#
# What a release publishes:
#   camillia-chat-server-heltec-v4-vX.Y.Z.bin   merged factory image, written at 0x0
#   flash.sh                                     helper that writes it with esptool
#
# There is no OTA image and no signing key: the chat server is updated over USB.
# Flashing the factory image leaves NVS (settings) and LittleFS (stored
# messages) alone — both sit above the end of the image.

set -e

RELEASE_ENVS=(heltec-v4 heltec-v4-expansion)
TEST_ENV="native"

env_out_name()          { echo "$1"; }
env_chip()              { echo "esp32s3"; }
env_flash_size()        { echo "16MB"; }
env_flash_mode()        { echo "dio"; }
env_bootloader_offset() { echo "0x0"; }

has_env() {
    grep -q "^\[env:$1\]" platformio.ini
}

remote_tag_exists() {
    git ls-remote --exit-code --tags origin "refs/tags/$1" >/dev/null 2>&1
}

delete_existing_release_and_tags() {
    local tag="$1"
    if gh release view "$tag" >/dev/null 2>&1; then
        echo "Deleting existing GitHub release $tag..."
        gh release delete "$tag" -y
    fi
    if remote_tag_exists "$tag"; then
        git push origin ":refs/tags/$tag"
    fi
    if git tag | grep -q "^${tag}$"; then
        git tag -d "$tag"
    fi
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

# ── Parse flags ───────────────────────────────────────────────────────────────
ASSUME_YES=false
ALPHA=false
VERSION_ARG=""
NO_CLEAN=false
APPEND_LAST_NOTES=false
NOTES_ONLY=false
USE_COMMITTED_NOTES=false
BUILD_LOCAL=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            echo "Usage: $0 [-y|--yes] [--alpha] [--version V] [--no-clean]"
            echo "          [--append-last-notes] [--notes-only]"
            echo "          [--use-committed-notes] [--build-local]"
            echo ""
            echo "  -y, --yes   No prompts: auto-bump the patch version and accept the notes."
            echo "  --alpha     Cut an ALPHA release (vX.Y.Z-alpha.N, published as a prerelease)."
            echo "  --version V Exact version, e.g. 0.2.0 (or 0.2.0 with --alpha for -alpha.1)."
            echo "  --no-clean  Skip the PlatformIO fullclean before building."
            echo "  --append-last-notes"
            echo "              Keep RELEASE_NOTES.md and append this release under a heading."
            echo "  --notes-only"
            echo "              Write and review RELEASE_NOTES.md, then stop."
            echo "  --use-committed-notes"
            echo "              Publish the committed RELEASE_NOTES.md as-is."
            echo "  --build-local"
            echo "              Build and publish from this machine instead of dispatching"
            echo "              the GitHub workflow."
            exit 0
            ;;
        -y|--yes) ASSUME_YES=true ;;
        --alpha) ALPHA=true ;;
        --version)
            if [[ $# -lt 2 ]]; then echo "--version needs a value" >&2; exit 1; fi
            VERSION_ARG="$2"
            shift
            ;;
        --no-clean) NO_CLEAN=true ;;
        --append-last-notes) APPEND_LAST_NOTES=true ;;
        --notes-only) NOTES_ONLY=true ;;
        --use-committed-notes) USE_COMMITTED_NOTES=true ;;
        --build-local) BUILD_LOCAL=true ;;
        *)
            echo "Unknown argument: $1 (see --help)" >&2
            exit 1
            ;;
    esac
    shift
done

if [[ "$NOTES_ONLY" == true && "$USE_COMMITTED_NOTES" == true ]]; then
    echo "--notes-only writes the notes; --use-committed-notes reuses them." >&2
    echo "Pick one." >&2
    exit 1
fi

# ── Where does this release happen? ──────────────────────────────────────────
# GITHUB_ACTIONS puts the workflow's own run into building mode.
REMOTE=false
if [[ "$NOTES_ONLY" != true && "$BUILD_LOCAL" != true && "${GITHUB_ACTIONS:-}" != "true" ]]; then
    REMOTE=true
fi

# ── Catch up with the branch before doing anything ───────────────────────────
# The workflow pushes its own "Release <tag>" commit (the VERSION bump), so this
# clone is one commit behind after every remote release. Sync before picking the
# version and before committing anything.
if [[ "$REMOTE" == true ]]; then
    BRANCH="$(git rev-parse --abbrev-ref HEAD)"
    if [[ "$BRANCH" == "HEAD" ]]; then
        echo "Detached HEAD — check out a branch before releasing." >&2
        exit 1
    fi

    git fetch -q --tags origin 2>/dev/null || true
    if git rev-parse --verify -q "origin/$BRANCH" >/dev/null 2>&1; then
        BEHIND="$(git rev-list --count "HEAD..origin/$BRANCH" 2>/dev/null || echo 0)"
        if [[ "$BEHIND" != "0" ]]; then
            echo "$BRANCH is $BEHIND commit(s) behind origin — rebasing onto it."
            if ! git -c rebase.autoStash=true rebase "origin/$BRANCH"; then
                git rebase --abort 2>/dev/null || true
                echo "" >&2
                echo "Could not rebase $BRANCH onto origin/$BRANCH." >&2
                echo "Resolve it by hand, then rerun. Nothing has been changed." >&2
                exit 1
            fi
        fi
    fi
fi

# ── Version ───────────────────────────────────────────────────────────────────
CURRENT=$(cat VERSION 2>/dev/null | tr -d '\n')
PREV_TAG=$(git describe --tags --abbrev=0 2>/dev/null || echo "none")
echo "Current version: ${CURRENT:-unknown}"
echo "Latest git tag:  $PREV_TAG"
echo ""

latest_tag_matching() {
    local glob="$1" filter="$2"
    git tag --list "$glob" | grep -E "$filter" | sort -V | tail -1
}

version_ge() {
    [[ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" == "$1" ]]
}

if [[ -n "$VERSION_ARG" ]]; then
    VERSION="${VERSION_ARG#v}"
    if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$ ]]; then
        echo "--version must look like 0.2.0 or 0.2.0-alpha.1 (got '$VERSION_ARG')." >&2
        exit 1
    fi
    if [[ "$ALPHA" == true && "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        VERSION="${VERSION}-alpha.1"
    fi
    echo "Using requested version v$VERSION"
elif [[ "$ALPHA" == true ]]; then
    # vX.Y.Z-alpha.N, where X.Y.Z is the release the series is heading towards.
    PREV_ALPHA_TAG="$(latest_tag_matching 'v*-alpha.*' '^v[0-9]+\.[0-9]+\.[0-9]+-alpha\.[0-9]+$')"
    PREV_STABLE_TAG="$(latest_tag_matching 'v*' '^v[0-9]+\.[0-9]+\.[0-9]+$')"
    echo "Latest alpha tag:  ${PREV_ALPHA_TAG:-none}"
    echo "Latest stable tag: ${PREV_STABLE_TAG:-none}"
    echo ""

    ALPHA_BASE=""
    ALPHA_NUM=0
    if [[ "$PREV_ALPHA_TAG" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)-alpha\.([0-9]+)$ ]]; then
        ALPHA_BASE="${BASH_REMATCH[1]}"
        ALPHA_NUM="${BASH_REMATCH[2]}"
    fi
    NEXT_STABLE_BASE=""
    if [[ "$PREV_STABLE_TAG" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
        NEXT_STABLE_BASE="${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.$(( BASH_REMATCH[3] + 1 ))"
    fi

    if [[ "$ASSUME_YES" == true ]]; then
        if [[ -n "$ALPHA_BASE" ]] \
           && { [[ -z "$NEXT_STABLE_BASE" ]] || version_ge "$ALPHA_BASE" "$NEXT_STABLE_BASE"; }; then
            VERSION="${ALPHA_BASE}-alpha.$(( ALPHA_NUM + 1 ))"
            echo "Auto-bumped $PREV_ALPHA_TAG -> v$VERSION"
        elif [[ -n "$NEXT_STABLE_BASE" ]]; then
            VERSION="${NEXT_STABLE_BASE}-alpha.1"
            echo "Starting a new alpha series after $PREV_STABLE_TAG -> v$VERSION"
        else
            echo "Cannot auto-bump: no vMAJOR.MINOR.PATCH tag to base an alpha on." >&2
            echo "Pass --version (e.g. --alpha --version 0.1.0)." >&2
            exit 1
        fi
    else
        read -rp "New alpha version (e.g. 0.2.0-alpha.1, or 0.2.0 for -alpha.1): " VERSION
        if [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
            VERSION="${VERSION}-alpha.1"
            echo "Using v$VERSION"
        fi
    fi
elif [[ "$ASSUME_YES" == true ]]; then
    # From the latest tag, not VERSION: the tag is what was actually published.
    if [[ "$PREV_TAG" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
        VERSION="${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.$(( BASH_REMATCH[3] + 1 ))"
        echo "Auto-bumped $PREV_TAG -> v$VERSION"
    else
        echo "Cannot auto-bump: latest tag '$PREV_TAG' is not vMAJOR.MINOR.PATCH." >&2
        echo "Pass --version (the first release has no tag to bump)." >&2
        exit 1
    fi
else
    read -rp "New version (e.g. 0.2.0): " VERSION
fi
if [[ -z "$VERSION" ]]; then
    echo "No version entered. Aborting."
    exit 1
fi
if [[ "$ALPHA" == true && ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+-alpha\.[0-9]+$ ]]; then
    echo "Alpha version must look like 0.2.0-alpha.1 (got '$VERSION')." >&2
    exit 1
fi
TAG="v$VERSION"

# ── Preflight checks ──────────────────────────────────────────────────────────
if ! command -v gh >/dev/null 2>&1; then
    echo "Error: GitHub CLI (gh) is required. Install from https://cli.github.com/ and run: gh auth login" >&2
    exit 1
fi

if [[ "$REMOTE" == true || "$NOTES_ONLY" == true ]]; then
    PIO=""; ESPTOOL=""; BOOT_APP0=""
else
    # The build does not copy boot_app0.bin into .pio/build; it ships with the
    # Arduino framework package, which exists once anything has been built (or
    # `pio pkg install` has run, which the workflow does).
    BOOT_APP0=$(find ~/.platformio/packages/framework-arduinoespressif32/tools/partitions \
        -name boot_app0.bin 2>/dev/null | head -1)
    if [[ -z "$BOOT_APP0" ]]; then
        echo "Error: boot_app0.bin not found in PlatformIO packages." >&2
        echo "Run 'pio pkg install -e heltec-v4' (or any build) first." >&2
        exit 1
    fi

    if [[ -x "$HOME/.platformio/penv/bin/pio" ]]; then
        PIO="$HOME/.platformio/penv/bin/pio"
    elif command -v pio >/dev/null 2>&1; then
        PIO="pio"
    else
        echo "Error: platformio not found. Run: pip install platformio" >&2
        exit 1
    fi

    if python -m esptool version >/dev/null 2>&1; then
        ESPTOOL="python -m esptool"
    elif command -v esptool.py >/dev/null 2>&1; then
        ESPTOOL="esptool.py"
    else
        echo "Error: esptool not found. Run: pip install esptool" >&2
        exit 1
    fi
fi

# ── Recreating an existing release? ──────────────────────────────────────────
# The deletion waits until the replacement is built and pushed.
RECREATE_RELEASE=false
if [[ "$REMOTE" != true && "$NOTES_ONLY" != true ]]; then
    if remote_tag_exists "$TAG" || git tag | grep -q "^${TAG}$"; then
        RECREATE_RELEASE=true
        echo "Tag ${TAG} already exists — it will be replaced once the build succeeds."
    fi
fi

# ── Failure guard ─────────────────────────────────────────────────────────────
# VERSION and RELEASE_NOTES.md are written before the build (the build bakes
# VERSION into the firmware). Roll them back on any failure before the release
# commit exists.
GUARD_FILES=(VERSION RELEASE_NOTES.md)
GUARD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/camillia-cs-release.XXXXXX")"
GUARD_ARMED=true
GUARD_STAGED=false

guard_snapshot() {
    local f
    for f in "${GUARD_FILES[@]}"; do
        if [[ -f "$f" ]]; then cp -p "$f" "$GUARD_DIR/$f"; else : > "$GUARD_DIR/$f.absent"; fi
    done
}

guard_rollback() {
    local f
    for f in "${GUARD_FILES[@]}"; do
        if [[ -f "$GUARD_DIR/$f.absent" ]]; then
            rm -f "$f"
        elif [[ -f "$GUARD_DIR/$f" ]]; then
            cp -p "$GUARD_DIR/$f" "$f"
        fi
    done
    if [[ "$GUARD_STAGED" == true ]]; then
        git add -- "${GUARD_FILES[@]}" 2>/dev/null || true
    fi
}

on_exit() {
    local status=$?
    if [[ "$GUARD_ARMED" == true && $status -ne 0 ]]; then
        echo "" >&2
        echo "Release failed (exit $status) — rolling back." >&2
        guard_rollback || true
        echo "Restored: ${GUARD_FILES[*]}" >&2
        echo "Nothing was committed, tagged, or published." >&2
    fi
    rm -rf "$GUARD_DIR"
}
trap on_exit EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

guard_snapshot

if [[ "$NOTES_ONLY" != true && "$REMOTE" != true ]]; then
    echo "$TAG" > VERSION
    echo "Updated VERSION to $TAG"
fi

# ── Release notes ─────────────────────────────────────────────────────────────
# Summarises the commits (and uncommitted diff) since the last tag. Prefers the
# Claude API (ANTHROPIC_API_KEY, works in CI), falls back to the local `claude`
# CLI. Every failure path is non-fatal for a local build: the release still
# publishes with GitHub's generated notes.
NOTES_FILE="RELEASE_NOTES.md"
PREV_NOTES=""
if [[ -f "$NOTES_FILE" ]]; then
    PREV_NOTES=$(cat "$NOTES_FILE")
fi

write_release_notes() {
    local body="$1"
    if [[ "$APPEND_LAST_NOTES" == true && -n "${PREV_NOTES//[[:space:]]/}" ]]; then
        {
            printf '%s\n' "$PREV_NOTES"
            printf '\n\n### Update (%s)\n' "$TAG"
            printf '%s\n' "$body"
        } > "$NOTES_FILE"
    else
        printf '%s\n' "$body" > "$NOTES_FILE"
    fi
}

write_placeholder_notes() {
    write_release_notes "Release ${TAG}

No release notes were generated for this build."
}

generate_ai_summary() {
    local range base log diffstat diff untracked prompt
    if [[ -n "$PREV_TAG" && "$PREV_TAG" != "none" ]]; then
        range="${PREV_TAG}..HEAD"
        base="$PREV_TAG"
    else
        range="HEAD"
        base="$(git rev-list --max-parents=0 HEAD | tail -1)"
    fi

    log=$(git log "$range" --no-merges --pretty=format:'- %s' 2>/dev/null | head -200 || true)
    diffstat=$(git diff --stat "$base" 2>/dev/null | tail -40 || true)
    diff=$(git diff "$base" --unified=0 \
              -- . ':(exclude)dist' ':(exclude)*.bin' ':(exclude)docs' \
              2>/dev/null | head -2000 || true)
    untracked=$(git ls-files --others --exclude-standard 2>/dev/null | head -40 || true)

    [[ -n "$log" || -n "$diff" ]] || return 1

    prompt="You are writing release notes for the camillia chat server, firmware for
a Heltec WiFi LoRa 32 V4 that stores Meshtastic channel messages (heard over
LoRa and MQTT) and serves them to camillia-mt nodes that missed them.

Summarize what changed in release ${TAG} for the people who flash and run it.

Rules:
- Group under '### New', '### Changed', '### Fixed'. Omit any empty section.
- One line per user-visible change, written for someone running the server, not a developer.
- Skip pure refactors, dependency bumps, version bumps, and release chores.
- Commit subjects are often terse. When one is, work out what changed from the
  diff instead. If you still cannot tell what it means for a user, leave it out silently.
- The release commit has not been made yet, so some of this release's work may
  be uncommitted. The diff is the authoritative record of what changed.
- This text is published verbatim. Never address the reader or the requester,
  never ask questions, and never explain what you omitted or why. Output nothing
  but the section headers and their bullet lines.

Commits since ${PREV_TAG} (may be empty):
${log}

Files changed (committed and uncommitted):
${diffstat}

New files not yet tracked by git:
${untracked}

Diff (zero context, truncated):
${diff}"

    if [[ -n "${ANTHROPIC_API_KEY:-}" ]]; then
        jq -n --arg p "$prompt" \
            '{model:"claude-opus-5",
              max_tokens:16000,
              thinking:{type:"adaptive"},
              messages:[{role:"user",content:$p}]}' 2>/dev/null \
        | curl -sS --max-time 180 https://api.anthropic.com/v1/messages \
            -H "content-type: application/json" \
            -H "x-api-key: ${ANTHROPIC_API_KEY}" \
            -H "anthropic-version: 2023-06-01" \
            --data @- 2>/dev/null \
        | jq -r '.content[]? | select(.type=="text") | .text' 2>/dev/null || true
    elif command -v claude >/dev/null 2>&1; then
        claude -p "$prompt" 2>/dev/null || true
    else
        return 1
    fi
}

review_notes_interactively() {
    local ans
    [[ -t 0 && "$ASSUME_YES" != true ]] || return 0
    while true; do
        read -rp "Use these notes? [Y]es / [e]dit / [n]o: " ans || ans="y"
        case "${ans:-y}" in
            y|Y|yes|Yes) return 0 ;;
            e|E|edit)
                "${EDITOR:-vi}" "$NOTES_FILE" || true
                echo ""
                echo "──────── edited release notes ────────"
                cat "$NOTES_FILE"
                echo "──────────────────────────────────────"
                ;;
            n|N|no) return 1 ;;
            *) echo "Please answer y, e, or n." ;;
        esac
    done
}

NOTES_ARGS=(--generate-notes)
if [[ "$USE_COMMITTED_NOTES" == true ]]; then
    if [[ -s "$NOTES_FILE" ]]; then
        echo ""
        echo "Using committed release notes:"
        echo "──────── release notes ────────"
        cat "$NOTES_FILE"
        echo "───────────────────────────────"
        NOTES_ARGS=(--notes-file "$NOTES_FILE" --generate-notes)
    else
        write_placeholder_notes
        echo "No committed release notes found — using GitHub's generated notes only."
    fi
else
    echo ""
    echo "Generating AI release summary..."
    AI_SUMMARY=$(generate_ai_summary || true)
    if [[ -n "${AI_SUMMARY// /}" ]]; then
        write_release_notes "$AI_SUMMARY"
        echo ""
        echo "──────── proposed release notes ────────"
        cat "$NOTES_FILE"
        echo "────────────────────────────────────────"

        if review_notes_interactively; then
            if [[ -s "$NOTES_FILE" ]]; then
                NOTES_ARGS=(--notes-file "$NOTES_FILE" --generate-notes)
            else
                write_placeholder_notes
                echo "Notes file is empty — using GitHub's generated notes only."
            fi
        elif [[ "$NOTES_ONLY" == true || "$REMOTE" == true ]]; then
            echo "Discarded. RELEASE_NOTES.md left as it was." >&2
            exit 1
        else
            write_placeholder_notes
            echo "Discarded — using GitHub's generated notes only."
        fi
    elif [[ "$NOTES_ONLY" == true || "$REMOTE" == true ]]; then
        echo "Could not generate release notes (no ANTHROPIC_API_KEY and no usable" >&2
        echo "'claude' CLI, or the request failed). RELEASE_NOTES.md left as it was." >&2
        exit 1
    else
        write_placeholder_notes
        echo "AI summary unavailable — falling back to GitHub's generated notes."
    fi
fi

if [[ "$ALPHA" == true ]]; then NOTES_CHANNEL=alpha; else NOTES_CHANNEL=stable; fi

if [[ "$NOTES_ONLY" == true ]]; then
    GUARD_ARMED=false
    echo ""
    echo "Release notes for $TAG written to $NOTES_FILE. Nothing built or published."
    echo ""
    echo "Next:"
    echo "  git add -A && git commit -m \"Prepare release $TAG\" && git push"
    echo "  gh workflow run release.yml -f channel=$NOTES_CHANNEL -f version=$VERSION"
    exit 0
fi

# ── Remote: commit, push, dispatch ───────────────────────────────────────────
if [[ "$REMOTE" == true ]]; then
    PENDING="$(git status --porcelain || true)"

    echo ""
    echo "Ready to release $TAG on GitHub:"
    echo "  branch:   $BRANCH"
    echo "  channel:  $NOTES_CHANNEL"
    echo "  notes:    $NOTES_FILE, published verbatim"
    echo ""
    if [[ -n "$PENDING" ]]; then
        echo "  Committing to $BRANCH, and releasing:"
        printf '%s\n' "$PENDING" | sed 's/^/     /'
    else
        echo "  Nothing to commit — releasing $BRANCH as it already stands."
    fi
    echo ""

    if [[ -t 0 && "$ASSUME_YES" != true ]]; then
        read -rp "Commit, push $BRANCH and dispatch? [y/N]: " ans || ans="n"
        case "${ans:-n}" in
            y|Y|yes|Yes) ;;
            *) echo "Aborted. RELEASE_NOTES.md will be restored." >&2; exit 1 ;;
        esac
    fi

    git add -A
    if git diff --cached --quiet; then
        echo "Nothing to commit."
    elif [[ "$ALPHA" == true ]]; then
        git commit -m "Prepare alpha release $TAG [skip ci]"
    else
        git commit -m "Prepare release $TAG [skip ci]"
    fi

    GUARD_ARMED=false

    git push || git push -u origin "$BRANCH"

    PREV_RUN=$(gh run list --workflow=release.yml --limit 1 \
               --json databaseId -q '.[0].databaseId' 2>/dev/null || true)

    gh workflow run release.yml --ref "$BRANCH" \
        -f channel="$NOTES_CHANNEL" \
        -f version="$VERSION" \
        -f notes=committed

    echo ""
    echo "Dispatched $TAG ($NOTES_CHANNEL) on $BRANCH. Waiting for the run..."
    RUN_ID=""
    for _ in $(seq 1 15); do
        sleep 2
        RUN_ID=$(gh run list --workflow=release.yml --limit 1 \
                 --json databaseId -q '.[0].databaseId' 2>/dev/null || true)
        [[ -n "$RUN_ID" && "$RUN_ID" != "$PREV_RUN" ]] && break
        RUN_ID=""
    done
    if [[ -n "$RUN_ID" ]]; then
        gh run watch "$RUN_ID" --exit-status || {
            echo "The release run failed: gh run view $RUN_ID --log-failed" >&2
            exit 1
        }
        git pull -q --ff-only || true
    else
        echo "Could not find the run; check the Actions tab."
    fi
    exit 0
fi

# ── Build firmware ────────────────────────────────────────────────────────────
echo ""
echo "Running native unit tests..."
"$PIO" test -e "$TEST_ENV"

echo ""
echo "Building firmware..."
for env_name in "${RELEASE_ENVS[@]}"; do
    if ! has_env "$env_name"; then
        echo "Release environment $env_name not found in platformio.ini" >&2
        exit 1
    fi
    if [[ "$NO_CLEAN" != true ]]; then
        "$PIO" run -e "$env_name" -t fullclean
    fi
    "$PIO" run -e "$env_name"
done

# ── Commit, push, and tag ─────────────────────────────────────────────────────
GUARD_STAGED=true
git add -A
if git diff --cached --quiet; then
    echo "Nothing to commit - VERSION already reads $TAG."
elif [[ "$ALPHA" == true ]]; then
    git commit -m "Alpha release $TAG [skip ci]"
else
    git commit -m "Release $TAG [skip ci]"
fi

GUARD_ARMED=false

git push

echo "Changes committed and pushed."

if [[ "$RECREATE_RELEASE" == true ]]; then
    delete_existing_release_and_tags "$TAG"
fi

if git tag | grep -q "^$TAG$"; then
    git tag -d "$TAG"
fi
git tag "$TAG"
git push origin "$TAG"

echo "Tag $TAG pushed."

# ── Merge factory images ──────────────────────────────────────────────────────
merge_assets() {
    local tag="$1" env_name out_name d out
    for env_name in "${RELEASE_ENVS[@]}"; do
        out_name="$(env_out_name "$env_name")"
        d=".pio/build/${env_name}"
        out="dist/camillia-chat-server-${out_name}-${tag}.bin"
        echo "  ${env_name} ($(env_flash_size "$env_name")) -> ${out}"
        $ESPTOOL --chip "$(env_chip "$env_name")" merge_bin \
            -o "${out}" \
            -fm "$(env_flash_mode "$env_name")" \
            -ff 80m \
            -fs "$(env_flash_size "$env_name")" \
            "$(env_bootloader_offset "$env_name")" "${d}/bootloader.bin" \
            0x8000  "${d}/partitions.bin" \
            0xe000  "$BOOT_APP0" \
            0x10000 "${d}/firmware.bin"
        cp "${d}/firmware.elf" "dist/camillia-chat-server-${out_name}-${tag}.elf"
    done
    install -m 0755 scripts/flash.sh dist/flash.sh
}

verify_assets() {
    local tag="$1" env_name factory
    echo "Verifying release assets..."
    for env_name in "${RELEASE_ENVS[@]}"; do
        factory="dist/camillia-chat-server-$(env_out_name "$env_name")-${tag}.bin"
        if [[ ! -s "$factory" ]]; then
            echo "Error: required release asset is missing or empty: $factory" >&2
            return 1
        fi
        echo "  OK ${env_name}: $(basename "$factory")"
    done
    if [[ ! -s dist/flash.sh || ! -x dist/flash.sh ]]; then
        echo "Error: dist/flash.sh is missing or not executable" >&2
        return 1
    fi
}

echo ""
echo "Merging factory images..."
rm -rf dist
mkdir -p dist
merge_assets "$TAG"
verify_assets "$TAG"
ls -lh dist/

# ── Create GitHub release ─────────────────────────────────────────────────────
# ELF symbol files stay out of the release; CI archives them as an artifact.
echo ""
echo "Creating GitHub release $TAG..."
RELEASE_FLAGS=()
if [[ "$ALPHA" == true ]]; then
    RELEASE_FLAGS+=( --prerelease )
fi
gh release create "$TAG" \
    --title "$TAG" \
    "${RELEASE_FLAGS[@]}" \
    "${NOTES_ARGS[@]}" \
    dist/*.bin \
    dist/*.sh

echo ""
if [[ "$ALPHA" == true ]]; then
    echo "Alpha release $TAG published (prerelease)."
else
    echo "Release $TAG published."
fi
gh release view "$TAG" --json url -q .url
