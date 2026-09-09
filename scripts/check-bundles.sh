#!/usr/bin/env bash
# Verifies the Qt sample applications are well-formed .app bundles.
set -uo pipefail

cd "$(dirname "$0")/.."

build_dir="src/cross/build/macos-arm64"
failed=0

fail() { printf 'FAIL %s\n' "$1"; failed=1; }
pass() { printf 'ok %s\n' "$1"; }
# Reports something true and worth seeing that is nobody's regression -- an exposure this
# project has accepted on purpose. Deliberately not spelled "ok": it is not a check passing.
note() { printf 'note %s\n' "$1"; }

# The floor every binary this project builds is compiled against, read from the preset rather
# than repeated here, so this check cannot drift away from what the build actually uses. The
# preset chain has to be walked: macos-arm64 inherits the value from macos-base.
deployment_target="$(python3 - src/cross/CMakePresets.json macos-arm64 <<'PRESET'
import json, sys

presets = {p["name"]: p for p in json.load(open(sys.argv[1]))["configurePresets"]}
name = sys.argv[2]
while name:
    preset = presets[name]
    value = preset.get("cacheVariables", {}).get("CMAKE_OSX_DEPLOYMENT_TARGET")
    if value:
        print(value)
        break
    inherits = preset.get("inherits")
    name = inherits[0] if isinstance(inherits, list) else inherits
PRESET
)"
if [[ -z "$deployment_target" ]]; then
    fail "no CMAKE_OSX_DEPLOYMENT_TARGET found in src/cross/CMakePresets.json for macos-arm64"
    exit "$failed"
fi

# minos, not the SDK version: the SDK a binary was built against says nothing about the oldest
# macOS it will start on, and LSMinimumSystemVersion is a claim about exactly that.
minos_of() { vtool -show-build "$1" 2>/dev/null | awk '/minos/ { print $2; exit }'; }

# Collects one line per app executable, for the cross-bundle comparison after the loop.
built_minos=""

for app in hex_viewer minidump remote_table release_notes regview; do
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
    #
    # Every Contents/Frameworks/*.framework binary carries its own LC_ID_DYLIB (self
    # install name), and macdeployqt leaves that one entry as the original absolute
    # Homebrew build path -- it rewrites the *references to* each framework in its
    # dependents to @rpath, never the framework's own id, because nothing resolves a
    # load at runtime by consulting a library's own id. `otool -L` prints that id as
    # the first line after the header, indistinguishable in shape from a real
    # dependency, so it has to be looked up via `otool -D` and excluded by exact match
    # -- otherwise this check fails every correctly-deployed bundle on its own
    # frameworks' harmless self-references instead of on an actual bad dependency.
    external=""
    while IFS= read -r -d '' macho; do
        rel="${macho#"$bundle"/}"
        self_id="$(otool -D "$macho" 2>/dev/null | tail -n +2)"
        bad="$(otool -L "$macho" 2>/dev/null | tail -n +2 | awk '{print $1}' \
            | grep -vE '^@(rpath|executable_path|loader_path)/' \
            | grep -vE '^/(usr/lib|System)/')"
        if [[ -n "$self_id" && -n "$bad" ]]; then
            bad="$(grep -vFx "$self_id" <<< "$bad")"
        fi
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

    # Deployment target, in the two places it can go wrong independently.
    #
    # One build once produced objects reporting 13.0 and 14.0 mixed together (issue #32); the
    # evidence was overwritten before anyone could read it, and no root cause was ever found.
    # What makes that worth guarding is not the warnings it produced, it is what such a build
    # would ship: a bundle whose LSMinimumSystemVersion promises one floor while its executable
    # needs a higher one starts on a machine it cannot run on, and the only symptom is a user's
    # crash on an older macOS. Neither half shows up in a build log, so both are asserted here
    # -- against the preset's floor, and against the plist's own claim.
    exe_minos="$(minos_of "$bundle/Contents/MacOS/$app")"
    if [[ -z "$exe_minos" ]]; then
        fail "$app.app executable reports no LC_BUILD_VERSION minos at all"
    elif [[ "$exe_minos" != "$deployment_target" ]]; then
        fail "$app.app executable targets macOS $exe_minos, but the build targets $deployment_target"
    else
        pass "$app.app executable targets macOS $exe_minos"
    fi
    if [[ -n "$exe_minos" ]]; then
        built_minos+="$exe_minos"$'\n'
    fi

    declared_minimum="$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$plist" 2>/dev/null)"
    if [[ -z "$declared_minimum" ]]; then
        fail "$app.app Info.plist has no LSMinimumSystemVersion"
    elif [[ -n "$exe_minos" && "$declared_minimum" != "$exe_minos" ]]; then
        fail "$app.app claims to run on macOS $declared_minimum but its executable needs $exe_minos"
    else
        pass "$app.app declares macOS $declared_minimum, matching its executable"
    fi

    # The libraries macdeployqt copied in are not this project's to recompile: Homebrew bottles
    # each formula against whatever macOS it was built on, so a bundle can ship Qt copies with a
    # higher floor than the app itself. That is a known consequence of building against Homebrew
    # Qt (docs/COMPILE-macos.md) and is why this reports rather than fails -- but it is reported
    # on every run, because it is the same shipping hazard the assertions above cover for our own
    # binaries, and it stops being acceptable the moment these bundles are given to anyone.
    shipped_max=""
    while IFS= read -r -d '' shipped; do
        shipped_minos="$(minos_of "$shipped")"
        [[ -z "$shipped_minos" ]] && continue
        if [[ -z "$shipped_max" ]] || \
           [[ "$(printf '%s\n%s\n' "$shipped_max" "$shipped_minos" | sort -V | tail -1)" == "$shipped_minos" ]]; then
            shipped_max="$shipped_minos"
        fi
    done < <(find "$bundle/Contents/Frameworks" "$bundle/Contents/PlugIns" -type f -print0 2>/dev/null)

    if [[ -n "$shipped_max" && -n "$declared_minimum" && "$shipped_max" != "$declared_minimum" ]] && \
       [[ "$(printf '%s\n%s\n' "$shipped_max" "$declared_minimum" | sort -V | tail -1)" == "$shipped_max" ]]; then
        note "$app.app declares macOS $declared_minimum but ships copied libraries needing up to $shipped_max (Homebrew Qt; see docs/COMPILE-macos.md)"
    fi
done

# The mixed-target symptom itself, which no single bundle can show: executables from one build
# reporting more than one floor between them. Every app passing its own comparison against the
# preset already implies agreement, so this can only fire alongside one of those failures -- it
# exists to name the shape of the fault, since "13.0 and 14.0 in one build" is the sentence that
# identifies issue #32 recurring rather than a preset someone edited.
distinct_minos="$(printf '%s' "$built_minos" | sed '/^$/d' | sort -u)"
# grep -c, not wc -l: `printf '%s'` emits no trailing newline, so wc -l undercounts by one and
# reported a single line for two distinct targets -- which is the one case this must catch.
if [[ "$(printf '%s\n' "$distinct_minos" | grep -c .)" -gt 1 ]]; then
    fail "one build produced executables with mixed deployment targets: $(printf '%s' "$distinct_minos" | tr '\n' ' ')"
fi

exit "$failed"
