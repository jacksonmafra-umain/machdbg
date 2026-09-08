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
    else
        printf 'warning: authored path is already absent, nothing to preserve: %s\n' "$path" >&2
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

# Some AUTHORED_PATHS entries are machdbg-only files upstream has no version of at all (the
# MachBug header, its tests, CMakePresets.json); restoring those wholesale is exactly right, and
# there is nothing upstream to compare them against. The rest -- src/cross/cmake.toml,
# src/cross/CMakeLists.txt, cmake/cmkr.cmake -- are upstream files carrying a permanent local
# edit, so they always differ from upstream's own copy of the file; diffing the restored file
# against the fresh clone would "fire" on every single run and teach the operator to ignore it.
# What actually matters is whether upstream's *own* version of the file changed since the last
# time it was vendored -- that is the change a wholesale restore silently throws away. So a copy
# of upstream's version of each such file is kept under upstream-refs/, tracked in git and
# overwritten every run (the same trick already used for the root upstream-cmake.toml /
# upstream-CMakeLists.txt reference copies above); comparing the fresh clone against the
# *previous* contents of that reference -- before this run overwrites it -- is what detects
# upstream drift. Which paths get this treatment falls out structurally, from whether the fresh
# clone has anything at all at that path, rather than from a second hardcoded list that could
# drift out of sync with AUTHORED_PATHS above.
#
# This is reported as a warning, not a failure: the copy and restore are already done by this
# point, there is nothing left for the script to safely undo, and the only real remedy is a
# human merge this script cannot perform itself. A hard failure here, on every re-vendor where
# upstream happens to touch one of these files, is the kind of failure an operator can't act on
# immediately and will learn to work around (e.g. by piping past a non-zero exit) -- which would
# bury the signal this exists to surface. So: finish the run, but make the warning impossible to
# miss, and keep both the old and new upstream copies on disk for the operator to diff.
diverged_count=0
diverged_dir=""
for path in "${AUTHORED_PATHS[@]}"; do
    upstream_copy="$work/x64dbg/$path"
    [[ -e "$upstream_copy" ]] || continue
    ref_path="$repo_root/upstream-refs/$path"
    mkdir -p "$(dirname "$ref_path")"
    if [[ -e "$ref_path" ]]; then
        if ! diff -q "$upstream_copy" "$ref_path" >/dev/null 2>&1; then
            if [[ -z "$diverged_dir" ]]; then
                diverged_dir="$(mktemp -d -t machdbg-vendor-diverged)"
            fi
            diverged_count=$((diverged_count + 1))
            old_dest="$diverged_dir/old/$path"
            new_dest="$diverged_dir/new/$path"
            mkdir -p "$(dirname "$old_dest")" "$(dirname "$new_dest")"
            cp "$ref_path" "$old_dest"
            cp "$upstream_copy" "$new_dest"
            {
                printf '\n'
                printf '################################################################################\n'
                printf '# UPSTREAM CHANGED A PRESERVED FILE: %s\n' "$path"
                printf '# Upstream'"'"'s own version of this file changed since the last vendor (now at\n'
                printf '# %s). The re-sync kept the local edit -- it is intact -- but whatever\n' "$resolved"
                printf '# upstream changed was NOT applied, and this is the only warning you get. See\n'
                printf '# what changed upstream, then fold it into the local edit by hand:\n'
                printf '#   diff %s %s\n' "$old_dest" "$new_dest"
                printf '################################################################################\n'
            } >&2
        fi
    else
        printf 'note: no prior upstream reference for %s; recording the current upstream version as the baseline for future re-syncs\n' "$path"
    fi
    cp "$upstream_copy" "$ref_path"
done

if [[ "$diverged_count" -gt 0 ]]; then
    printf '\nWARNING: %d preserved file(s) changed upstream this cycle and were not merged; see the banner(s) above. Old/new upstream copies kept at: %s\n' \
        "$diverged_count" "$diverged_dir" >&2
fi

printf 'done. resolved commit: %s\n' "$resolved"
