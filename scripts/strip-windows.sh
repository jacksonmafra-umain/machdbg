#!/usr/bin/env bash
# Removes vendored components that cannot run on macOS. Idempotent.
#
# src/gui/Src stays: the widget library compiles 78 of its files.
# Zydis stays until milestone 6: x64dbg_widgets still links zydis_wrapper.
# cmake/init-submodules.cmake is upstream's; our cmkr.cmake no longer includes it.
# It returns from upstream on every re-vendor and is dead weight, but looks load-bearing.
set -euo pipefail

cd "$(dirname "$0")/.."

paths=(
    cmake/init-submodules.cmake
    src/dbg/TitanEngine
    src/dbg/GleeBug
    src/dbg/XEDParse
    src/dbg/DeviceNameResolver
    src/exe
    src/launcher
    src/loaddll
)

for path in "${paths[@]}"; do
    if [[ -e "$path" ]]; then
        rm -rf "$path"
        printf 'removed %s\n' "$path"
    else
        printf 'absent  %s\n' "$path"
    fi
done
