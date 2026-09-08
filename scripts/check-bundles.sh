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

    icon="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "$plist" 2>/dev/null)"
    if [[ -z "$icon" ]]; then
        fail "$app.app Info.plist has no CFBundleIconFile"
    elif [[ ! -f "$bundle/Contents/Resources/$icon" ]]; then
        fail "$app.app declares icon $icon but Contents/Resources/$icon is missing"
    else
        pass "$app.app icon $icon"
    fi

    external="$(otool -L "$bundle/Contents/MacOS/$app" \
        | awk '/Qt[A-Za-z]*\.framework/ && $1 !~ /^@(executable_path|rpath|loader_path)/ {print $1}')"
    if [[ -n "$external" ]]; then
        fail "$app.app resolves Qt outside the bundle: $(echo "$external" | tr '\n' ' ')"
    else
        pass "$app.app Qt resolves inside the bundle"
    fi

    # A structural pass alone missed the very bug this project was built to expose:
    # macdeployqt strips its own copied files after signing them, invalidating the
    # signature, and every earlier check here is blind to it -- only launching the
    # bundle (or asking codesign directly) can see it. Qt.cmake re-signs and verifies
    # at build time, but that only protects whoever runs that exact build; this check
    # gives the same signal to anyone who inherits a build tree without rebuilding it.
    signature_error="$(codesign --verify --deep --strict "$bundle" 2>&1 >/dev/null)"
    if [[ -n "$signature_error" ]]; then
        fail "$app.app has an invalid code signature: $(echo "$signature_error" | tr '\n' ' ')"
    else
        pass "$app.app code signature verifies"
    fi
done

exit "$failed"
