#!/bin/bash
set -euo pipefail

# check-build-warnings.sh: Verify that a clean build of the Qt targets produces only
# the known, bounded set of linker warnings from QtSvg and QtWebSockets.
#
# These two frameworks are bottled against macOS 26.0 on Homebrew while qtbase targets
# 14.0. The floor stays at 14.0 to avoid setting the minimum OS to 26.0 in bundles.
# This script ensures no new warnings appear and the exception remains accurate.

REPO_ROOT="$(git rev-parse --show-toplevel)"
TEMP_BUILD="$(mktemp -d)"
trap "rm -rf '$TEMP_BUILD'" EXIT

cd "$REPO_ROOT/src/cross"

echo "Building into temporary directory: $TEMP_BUILD"
export QT_ROOT_DIR="${QT_ROOT_DIR:-$(brew --prefix qt)}"

# Configure and build all four Qt targets
echo "Configuring CMake..."
cmake --preset macos-arm64 -B "$TEMP_BUILD" >/dev/null

echo "Building all four targets (hex_viewer, minidump, remote_table, release_notes)..."
cmake --build "$TEMP_BUILD" --target hex_viewer minidump remote_table release_notes 2>&1 \
  | tee /tmp/build_warnings.log >/dev/null

# Extract libraries from warnings
echo "Analyzing warnings..."
WARNINGS=$(grep -o "linking with dylib '[^']*'" /tmp/build_warnings.log | sed "s/.*\/\([^/]*\)\.framework.*/\1/" | sort -u || true)

# Expected libraries
EXPECTED="QtSvg
QtWebSockets"

if [ -z "$WARNINGS" ]; then
  echo "ok no linker warnings"
  exit 0
fi

# Compare
ACTUAL=$(echo "$WARNINGS" | sort)
if [ "$ACTUAL" = "$EXPECTED" ]; then
  echo "ok linker warnings confined to QtSvg and QtWebSockets (as expected)"
  exit 0
else
  echo "FAIL linker warnings from unexpected libraries:"
  comm -23 <(echo "$ACTUAL") <(echo "$EXPECTED") | sed 's/^/  /'
  echo ""
  echo "Expected only:"
  echo "$EXPECTED" | sed 's/^/  /'
  exit 1
fi
