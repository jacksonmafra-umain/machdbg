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

# A fixed log path is overwritten by the very next run, destroying the evidence a surprising
# result needs to be investigated. Give each invocation its own path — and, unlike TEMP_BUILD,
# do not delete it on exit; it is meant to outlive this run.
LOG_FILE="$(mktemp /tmp/build_warnings.log.XXXXXX)"
echo "Full build log: $LOG_FILE"

cd "$REPO_ROOT/src/cross"

echo "Building into temporary directory: $TEMP_BUILD"
export QT_ROOT_DIR="${QT_ROOT_DIR:-$(brew --prefix qt)}"

# Configure and build all four Qt targets
echo "Configuring CMake..."
cmake --preset macos-arm64 -B "$TEMP_BUILD" >/dev/null

echo "Building all four targets (hex_viewer, minidump, remote_table, release_notes)..."
cmake --build "$TEMP_BUILD" --target hex_viewer minidump remote_table release_notes 2>&1 \
  | tee "$LOG_FILE" >/dev/null

# Extract libraries from warnings
echo "Analyzing warnings..."
WARNINGS=$(grep -o "linking with dylib '[^']*'" "$LOG_FILE" | sed "s/.*\/\([^/]*\)\.framework.*/\1/" | sort -u || true)

# Expected libraries
EXPECTED="QtSvg
QtWebSockets"

# Normalise both sets: no blank lines, sorted, deduplicated. An empty observed set
# is a legitimate value here, not a shortcut to success: if QtSvg and QtWebSockets
# stop warning, the documented exception has outlived its cause and must be removed,
# so that case has to fail too.
ACTUAL=$(printf '%s\n' "$WARNINGS" | sed '/^[[:space:]]*$/d' | sort -u)
EXPECTED=$(printf '%s\n' "$EXPECTED" | sed '/^[[:space:]]*$/d' | sort -u)

if [ "$ACTUAL" = "$EXPECTED" ]; then
  echo "ok linker warnings confined to QtSvg and QtWebSockets (as expected)"
  exit 0
fi

# comm needs real files/streams; feed it the normalised sets without reintroducing
# the blank line that `echo ""` would emit for an empty set.
UNEXPECTED=$(comm -23 <(printf '%s\n' "$ACTUAL" | sed '/^$/d') <(printf '%s\n' "$EXPECTED" | sed '/^$/d'))
MISSING=$(comm -13 <(printf '%s\n' "$ACTUAL" | sed '/^$/d') <(printf '%s\n' "$EXPECTED" | sed '/^$/d'))

echo "FAIL the observed linker warnings do not match the documented exception."

if [ -n "$UNEXPECTED" ]; then
  echo ""
  echo "Libraries that warned but are not part of the exception:"
  printf '%s\n' "$UNEXPECTED" | sed 's/^/  /'
fi

if [ -n "$MISSING" ]; then
  echo ""
  echo "Libraries the exception expects to warn, but which did not:"
  printf '%s\n' "$MISSING" | sed 's/^/  /'
  if [ -z "$ACTUAL" ]; then
    echo ""
    echo "No linker warnings were produced at all. The exception documented in"
    echo "docs/COMPILE-macos.md looks stale: if QtSvg and QtWebSockets no longer"
    echo "warn, delete the exception and this check instead of leaving them behind."
  else
    echo ""
    echo "The exception documented in docs/COMPILE-macos.md looks partly stale and"
    echo "should be re-examined."
  fi
fi

echo ""
echo "Observed:"
if [ -n "$ACTUAL" ]; then printf '%s\n' "$ACTUAL" | sed 's/^/  /'; else echo "  (none)"; fi
echo "Expected:"
printf '%s\n' "$EXPECTED" | sed 's/^/  /'
echo ""
echo "Full build log kept at: $LOG_FILE"
exit 1
