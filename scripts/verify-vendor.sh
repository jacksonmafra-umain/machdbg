#!/usr/bin/env bash
# Verifies the vendored upstream tree is present and complete. Exit 0 when it is.
set -uo pipefail

cd "$(dirname "$0")/.."

failed=0

fail() { printf 'FAIL %s\n' "$1"; failed=1; }
pass() { printf 'ok %s\n' "$1"; }

# The pinned commit must be recorded, and it must be the one the design document names.
if [[ ! -f docs/upstream.md ]]; then
    fail "docs/upstream.md is missing"
elif ! grep -q '8794998' docs/upstream.md; then
    fail "docs/upstream.md does not record the pinned commit 8794998"
else
    pass "pinned commit recorded"
fi

# Files the build cannot do without.
required=(
    src/cross/cmake.toml
    src/cross/widgets/CMakeLists.txt
    src/cross/widgets/Qt.cmake
    src/cross/ElfBug/ElfBug/api/elfbug_api.h
    src/cross/ElfBug/tests/TestHarness.h
    src/bridge/bridgemain.h
    src/dbg/_plugins.h
    cmake/cmkr.cmake
)
for path in "${required[@]}"; do
    if [[ -e "$path" ]]; then
        pass "$path"
    else
        fail "$path is missing"
    fi
done

exit "$failed"
