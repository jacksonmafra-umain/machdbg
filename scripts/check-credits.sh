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
            "Pedro Vila" StackContains DrDecode VisualPharm Fugue GleeBug; do
    require_in CREDITS.md "$name"
done

for component in GPLv3 "Qt 6" Capstone asmjit jansson lz4 yara Zydis; do
    require_in docs/licenses.md "$component"
done

require_in README.md CREDITS.md

# The plugin exception is a named project constraint, not just prose: both files must state it,
# and identically, or the two copies drift apart again. The sentence is wrapped across multiple
# lines to fit each file's prose width, so whitespace (including the line break) is normalised
# to single spaces before comparing rather than grepping for it on one line.
plugin_exception="Plugins may be closed-source, commercial or private, unless they copy code from machdbg or x64dbg."
for file in README.md docs/licenses.md; do
    if [[ ! -f "$file" ]]; then
        printf 'FAIL %s is missing\n' "$file"
        failed=1
        continue
    fi
    normalized="$(tr '\n\t' '  ' < "$file" | tr -s ' ')"
    if [[ "$normalized" == *"$plugin_exception"* ]]; then
        printf 'ok %s states the plugin exception\n' "$file"
    else
        printf 'FAIL %s does not state the plugin exception verbatim\n' "$file"
        failed=1
    fi
done

exit "$failed"
