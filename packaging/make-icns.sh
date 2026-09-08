#!/usr/bin/env bash
# Builds packaging/machdbg.icns from src/bug.png. Run when the source image changes.
# The result is committed, so building machdbg does not require sips or iconutil.
set -euo pipefail

cd "$(dirname "$0")/.."

source_png="src/bug.png"
iconset="$(mktemp -d)/machdbg.iconset"
mkdir -p "$iconset"

# src/bug.png is 256x256, so 256 is the largest honest size and 128@2x is the
# largest honest retina variant. Asking sips for 512 or 1024 would upscale.
for size in 16 32 128; do
    sips -z "$size" "$size" "$source_png" --out "$iconset/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips -z "$double" "$double" "$source_png" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
sips -z 256 256 "$source_png" --out "$iconset/icon_256x256.png" >/dev/null

iconutil --convert icns "$iconset" --output packaging/machdbg.icns
rm -rf "$(dirname "$iconset")"

printf 'wrote packaging/machdbg.icns (%s bytes)\n' "$(stat -f%z packaging/machdbg.icns)"
