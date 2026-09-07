#!/usr/bin/env bash
# Verifies the macOS build toolchain. Exit 0 when complete, 1 when anything is missing.
set -uo pipefail

missing=0

require_version() {
    local tool="$1" min="$2" version_cmd="$3" hint="$4"
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'missing %s — %s\n' "$tool" "$hint"
        missing=1
        return
    fi
    local version
    version="$(eval "$version_cmd" 2>/dev/null | head -1)"
    if [[ -z "$version" ]]; then
        printf 'missing %s — installed but version unreadable; %s\n' "$tool" "$hint"
        missing=1
        return
    fi
    # Sorts the two versions and checks the minimum is not the greater of the pair.
    if [[ "$(printf '%s\n%s\n' "$min" "$version" | sort -V | head -1)" != "$min" ]]; then
        printf 'missing %s — found %s, need %s or newer; %s\n' "$tool" "$version" "$min" "$hint"
        missing=1
        return
    fi
    printf 'ok %s %s\n' "$tool" "$version"
}

require_present() {
    local tool="$1" hint="$2"
    if command -v "$tool" >/dev/null 2>&1; then
        printf 'ok %s present\n' "$tool"
    else
        printf 'missing %s — %s\n' "$tool" "$hint"
        missing=1
    fi
}

require_version cmake 3.19 "cmake --version | awk 'NR==1{print \$3}'" "brew install cmake"
require_version ninja 1.10 "ninja --version" "brew install ninja"
require_present cmkr "not in Homebrew; see docs/COMPILE-macos.md"
require_present codesign "install the Xcode command line tools: xcode-select --install"

if [[ -z "${QT_ROOT_DIR:-}" ]]; then
    printf 'missing Qt6 — set QT_ROOT_DIR to a Qt 6 macOS installation; see docs/COMPILE-macos.md\n'
    missing=1
elif [[ ! -x "$QT_ROOT_DIR/bin/qmake" ]]; then
    printf 'missing Qt6 — QT_ROOT_DIR=%s has no bin/qmake\n' "$QT_ROOT_DIR"
    missing=1
else
    printf 'ok Qt6 %s\n' "$("$QT_ROOT_DIR/bin/qmake" -query QT_VERSION)"
fi

exit "$missing"
