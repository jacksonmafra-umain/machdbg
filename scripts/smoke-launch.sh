#!/usr/bin/env bash
# Launches each bundle, confirms it stays alive, then terminates it.
# Set MACHDBG_SMOKE_OFFSCREEN=1 where no window server is available.
set -uo pipefail

cd "$(dirname "$0")/.."

build_dir="src/cross/build/macos-arm64"
failed=0

fail() { printf 'FAIL %s\n' "$1"; failed=1; }
pass() { printf 'ok %s\n' "$1"; }

if [[ "${MACHDBG_SMOKE_OFFSCREEN:-0}" == "1" ]]; then
    export QT_QPA_PLATFORM=offscreen
fi

for app in hex_viewer minidump remote_table release_notes regview; do
    binary="$build_dir/$app.app/Contents/MacOS/$app"
    if [[ ! -x "$binary" ]]; then
        fail "$app has no executable to launch"
        continue
    fi

    # release_notes requires a markdown file argument (see its main(), which prints a
    # usage message and exits immediately without one) -- launching it bare tests only
    # that we called it wrong, not whether it works. README.md is a real file already in
    # the tree, so this at least parses real markdown instead of a fixture nobody
    # maintains, rather than exercising every downstream rendering path. No other app
    # takes an argument.
    #
    # Positional parameters (set -- / "$@"), not an array, carry that argument: under
    # `set -u`, expanding an empty array as "${args[@]}" is an unbound-variable error on
    # bash 3.2 -- the only bash /bin/bash actually is on macos-15 runners, regardless of
    # what a Homebrew bash on PATH makes `#!/usr/bin/env bash` pick up locally. "$@" has
    # no such problem even with zero positional parameters, on any bash version.
    set --
    if [[ "$app" == "regview" ]]; then
        # regview launches or attaches to a target; without one it shows an empty window, which
        # proves less than the same launch with a fixture it can actually stop. The fixture is
        # only there when the engine tests were built, so its absence is a skip rather than a
        # failure -- this script's job is that the bundles start, not that the test targets exist.
        fixture="$build_dir/tests/targets/run_endlessly"
        if [[ -x "$fixture" ]]; then
            set -- "$fixture"
        fi
    fi
    if [[ "$app" == "release_notes" ]]; then
        md_file="README.md"
        if [[ ! -f "$md_file" ]]; then
            fail "$app has no $md_file to smoke-test against"
            continue
        fi
        set -- "$md_file"
    fi

    log="$(mktemp)"
    "$binary" "$@" >"$log" 2>&1 &
    pid=$!

    # Give the application time to reach its first window or die trying.
    for _ in $(seq 1 20); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.25
    done

    if kill -0 "$pid" 2>/dev/null; then
        pass "$app stayed alive"
        kill "$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
    else
        wait "$pid" 2>/dev/null
        status=$?
        hint=""
        if [[ "$status" -eq 137 ]]; then
            # 137 = 128+SIGKILL. A SIGKILL leaves no stderr to explain itself -- this
            # exact code and empty log is what an invalid code signature looked like
            # (the kernel's code-signing enforcement kills the process on first page-in
            # of a tainted dylib, before it can print anything). Diagnose with
            # `codesign -vvv --deep` on the bundle and `log show` for
            # "CODE SIGNING: cs_invalid_page".
            hint=" (SIGKILL with no output: check the bundle's code signature with 'codesign -vvv --deep', a likely cause)"
        fi
        fail "$app exited early with status $status: $(head -3 "$log" | tr '\n' ' ')$hint"
    fi
    rm -f "$log"
done

exit "$failed"
