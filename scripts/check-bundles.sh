#!/usr/bin/env bash
# Verifies the Qt sample applications are well-formed .app bundles.
set -uo pipefail

cd "$(dirname "$0")/.."

build_dir="src/cross/build/macos-arm64"
failed=0

fail() { printf 'FAIL %s\n' "$1"; failed=1; }
pass() { printf 'ok %s\n' "$1"; }

for app in hex_viewer minidump remote_table release_notes; do
    bundle="$build_dir/$app.app"
    if [[ ! -d "$bundle" ]]; then
        fail "$app.app was not produced"
        continue
    fi
    if [[ ! -x "$bundle/Contents/MacOS/$app" ]]; then
        fail "$app.app has no executable at Contents/MacOS/$app"
        continue
    fi
    plist="$bundle/Contents/Info.plist"
    if [[ ! -f "$plist" ]]; then
        fail "$app.app has no Info.plist"
        continue
    fi
    identifier="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist" 2>/dev/null)"
    if [[ -z "$identifier" ]]; then
        fail "$app.app Info.plist has no CFBundleIdentifier"
    else
        pass "$app.app ($identifier)"
    fi
done

exit "$failed"
