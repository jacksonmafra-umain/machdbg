#!/usr/bin/env bash
# Launches each bundle, confirms it stays alive, then terminates it.
# Set MACHDBG_SMOKE_OFFSCREEN=1 where no window server is available.
set -uo pipefail

cd "$(dirname "$0")/.."

build_dir="src/cross/build/macos-arm64"
failed=0

fail() { printf 'FAIL %s\n' "$1"; failed=1; }
pass() { printf 'ok %s\n' "$1"; }

if [[ "${MACHDBG_SMOKE_OFFSCREEN:-0}" == "1" ]]; then
    export QT_QPA_PLATFORM=offscreen
fi

for app in hex_viewer minidump; do
    binary="$build_dir/$app.app/Contents/MacOS/$app"
    if [[ ! -x "$binary" ]]; then
        fail "$app has no executable to launch"
        continue
    fi

    log="$(mktemp)"
    "$binary" >"$log" 2>&1 &
    pid=$!

    # Give the application time to reach its first window or die trying.
    for _ in $(seq 1 20); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.25
    done

    if kill -0 "$pid" 2>/dev/null; then
        pass "$app stayed alive"
        kill "$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
    else
        wait "$pid" 2>/dev/null
        status=$?
        fail "$app exited early with status $status: $(head -3 "$log" | tr '\n' ' ')"
    fi
    rm -f "$log"
done

exit "$failed"
