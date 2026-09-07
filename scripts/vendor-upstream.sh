#!/usr/bin/env bash
# Copies the upstream x64dbg tree at the pinned commit into this repository.
# Re-runnable: it overwrites the vendored paths and leaves machdbg-only files alone.
set -euo pipefail

UPSTREAM_URL="https://github.com/x64dbg/x64dbg.git"
UPSTREAM_BRANCH="development"
UPSTREAM_COMMIT="8794998"

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

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

printf 'done. resolved commit: %s\n' "$resolved"
