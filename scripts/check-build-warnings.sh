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

# Removed on success only. A failing run's build directory holds build.ninja and the exact link
# commands, which is the evidence issue #32 needed and did not have: the one time deployment
# targets came back mixed, the directory and the log were both gone before anyone could read
# them, and three clean reruns proved only that a later build was clean.
keep_evidence=0
cleanup() {
    if [[ "$keep_evidence" -eq 1 ]]; then
        echo "Build directory kept for investigation: $TEMP_BUILD"
        return
    fi
    rm -rf "$TEMP_BUILD"
}
trap cleanup EXIT

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

# The deployment target every produced binary reports, which is the other half of issue #32.
# A mixed build is not a linker-warning problem and would otherwise pass this check entirely:
# the warnings could be exactly the documented two while the objects behind them were compiled
# against different minimums. minos, not the SDK version -- the SDK says nothing about the
# oldest OS a binary will run on.
echo "Checking deployment targets..."
target_minimums=""
while IFS= read -r binary; do
    minos="$(vtool -show-build "$binary" 2>/dev/null | awk '/minos/ { print $2; exit }')"
    [[ -z "$minos" ]] && continue
    target_minimums+="$minos"$'\n'
done < <(find "$TEMP_BUILD" -type f -perm -111 -name '*' -maxdepth 2 2>/dev/null | head -50)

distinct_minimums="$(printf '%s' "$target_minimums" | sed '/^$/d' | sort -u)"
if [[ "$(printf '%s' "$distinct_minimums" | grep -c .)" -gt 1 ]]; then
    keep_evidence=1
    echo "FAIL this build produced binaries with more than one deployment target:"
    printf '%s\n' "$distinct_minimums" | sed 's/^/  macOS /'
    echo ""
    echo "This is issue #32 reproducing. The build directory and the log below are the evidence"
    echo "that investigation has never had -- do not delete them."
    echo "Full build log kept at: $LOG_FILE"
    exit 1
fi
if [[ -n "$distinct_minimums" ]]; then
    echo "ok every produced binary targets macOS $distinct_minimums"
fi

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

keep_evidence=1
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
