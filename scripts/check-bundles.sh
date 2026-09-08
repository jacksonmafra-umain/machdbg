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

    # Checks every file the bundle ships under MacOS/, Frameworks/ and PlugIns/, not just
    # Contents/MacOS/$app. The main executable is the file macdeployqt rewrites most
    # carefully, so it is the least likely place a stale absolute path survives.
    # Contents/Frameworks/*.framework binaries are macdeployqt's own copies (also rewritten
    # by it), but Contents/PlugIns/platforms/libqoffscreen.dylib is hand-copied by Qt.cmake
    # and hand-given an rpath -- macdeployqt never touches it -- making it the single most
    # likely file to carry a wrong path.
    #
    # No Mach-O filter is needed before calling otool: run against a non-object file (an
    # Info.plist, a .xcprivacy file) it prints exactly one line, "<path>: is not an object
    # file", which `tail -n +2` (skipping what would be the header line on a real object
    # file) discards, leaving nothing for the following pipeline to flag.
    #
    # `find -type f` (not -L) walks real files only, skipping the *.framework/{Name,
    # Versions/Current} symlinks so each binary is inspected once.
    external=""
    while IFS= read -r -d '' macho; do
        rel="${macho#"$bundle"/}"
        bad="$(otool -L "$macho" 2>/dev/null | tail -n +2 | awk '{print $1}' \
            | grep -vE '^@(rpath|executable_path|loader_path)/' \
            | grep -vE '^/(usr/lib|System)/')"
        if [[ -n "$bad" ]]; then
            while IFS= read -r path; do
                external+="$rel -> $path; "
            done <<< "$bad"
        fi
    done < <(find "$bundle/Contents/MacOS" "$bundle/Contents/Frameworks" "$bundle/Contents/PlugIns" \
        -type f -print0 2>/dev/null)

    if [[ -n "$external" ]]; then
        fail "$app.app resolves Qt outside the bundle: $external"
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
