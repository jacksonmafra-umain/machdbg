#!/usr/bin/env bash
# Copies the upstream x64dbg tree at the pinned commit into this repository.
# Re-runnable: it overwrites the vendored src/ and cmake/ trees, then restores the
# machdbg-authored files listed in AUTHORED_PATHS, so a re-vendor cannot silently destroy
# them. See docs/upstream.md, "What was changed locally", for what each one carries.
set -euo pipefail

UPSTREAM_URL="https://github.com/x64dbg/x64dbg.git"
UPSTREAM_BRANCH="development"
UPSTREAM_COMMIT="8794998"

repo_root="$(cd "$(dirname "$0")/.." && pwd)"

# Files inside src/ and cmake/ that are machdbg-authored, or upstream files with a deliberate
# local edit. A plain rm -rf of src/ and cmake/ followed by an upstream copy would erase these;
# they are snapshotted before the copy and restored afterwards.
AUTHORED_PATHS=(
    "src/cross/MachBug/MachBug/api/machbug_api.h"
    "src/cross/MachBug/tests/CMakeLists.txt"
    "src/cross/MachBug/tests/api_contract.cpp"
    "src/cross/MachBug/tests/api_contract_c.c"
    "src/cross/MachBug/tests/cmake.toml"
    "src/cross/CMakePresets.json"
    "src/cross/cmake.toml"
    "src/cross/CMakeLists.txt"
    "cmake/cmkr.cmake"
)

# The restore step below overwrites the paths above unconditionally. Refuse to run against a
# dirty tree so a mistake here is always recoverable with a plain git checkout.
if [[ -n "$(git -C "$repo_root" status --porcelain)" ]]; then
    echo "refusing to run: working tree is not clean (commit or stash first)" >&2
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

preserve="$work/preserved"
mkdir -p "$preserve"
for path in "${AUTHORED_PATHS[@]}"; do
    if [[ -e "$repo_root/$path" ]]; then
        mkdir -p "$preserve/$(dirname "$path")"
        cp -R "$repo_root/$path" "$preserve/$path"
    fi
done

git clone --branch "$UPSTREAM_BRANCH" "$UPSTREAM_URL" "$work/x64dbg"
git -C "$work/x64dbg" checkout "$UPSTREAM_COMMIT"

resolved="$(git -C "$work/x64dbg" rev-parse HEAD)"
printf 'vendoring %s at %s\n' "$UPSTREAM_URL" "$resolved"

for path in src cmake; do
    rm -rf "${repo_root:?}/$path"
    cp -R "$work/x64dbg/$path" "$repo_root/$path"
done

# Upstream build manifests the vendored tree references.
for file in cmake.toml CMakeLists.txt; do
    cp "$work/x64dbg/$file" "$repo_root/upstream-$file"
done

find "$repo_root/src" "$repo_root/cmake" -name '.git' -prune -exec rm -rf {} +

# Restore the machdbg-authored files the copy above just overwrote or deleted.
for path in "${AUTHORED_PATHS[@]}"; do
    if [[ -e "$preserve/$path" ]]; then
        mkdir -p "$repo_root/$(dirname "$path")"
        rm -rf "$repo_root/$path"
        cp -R "$preserve/$path" "$repo_root/$path"
    fi
done

printf 'done. resolved commit: %s\n' "$resolved"
