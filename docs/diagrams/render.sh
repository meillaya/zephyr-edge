#!/usr/bin/env bash
# Render every committed .excalidraw scene to PNG with the official CLI.
#
# Requires `excalidraw` (https://github.com/excalidraw/cli) and a headless
# Chrome/Chromium/Firefox. The CLI uses HOME for its temporary browser profile,
# so render with a writable scratch HOME (the sandbox host home is read-only).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

command -v excalidraw >/dev/null || {
    echo "ERROR: excalidraw CLI not found on PATH" >&2
    exit 1
}

scratch="$(mktemp -d /tmp/excalidraw-render.XXXXXX)"
trap 'rm -rf "$scratch"' EXIT
mkdir -p "$scratch/.config" "$scratch/.cache"

for src in *.excalidraw; do
    out="${src%.excalidraw}.png"
    echo ">> $src -> $out"
    HOME="$scratch" XDG_CONFIG_HOME="$scratch/.config" XDG_CACHE_HOME="$scratch/.cache" \
        excalidraw render "$src" --out "$out" \
        --scale 1.5 --max-width 2048 --max-height 2048
 done
echo ">> diagrams rendered"
