#!/usr/bin/env bash
# Fails if a required credit or licence entry is missing.
set -uo pipefail

cd "$(dirname "$0")/.."

failed=0

require_in() {
    local file="$1" needle="$2"
    if [[ ! -f "$file" ]]; then
        printf 'FAIL %s is missing\n' "$file"
        failed=1
        return
    fi
    if grep -qi -- "$needle" "$file"; then
        printf 'ok %s mentions %s\n' "$file" "$needle"
    else
        printf 'FAIL %s does not mention %s\n' "$file" "$needle"
        failed=1
    fi
}

for name in mrexodia Sigma tr4ceflow Dreg Nukem Herz3h torusrxxx 3rdit eldarkg \
            "Pedro Vila" StackContains DrDecode VisualPharm Fugue; do
    require_in CREDITS.md "$name"
done

for component in GPLv3 "Qt 6" Capstone asmjit jansson lz4 yara; do
    require_in docs/licenses.md "$component"
done

require_in README.md CREDITS.md

exit "$failed"
