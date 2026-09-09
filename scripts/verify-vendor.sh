#!/usr/bin/env bash
# Verifies the vendored upstream tree is present and complete. Exit 0 when it is.
set -uo pipefail

cd "$(dirname "$0")/.."

failed=0

fail() { printf 'FAIL %s\n' "$1"; failed=1; }
pass() { printf 'ok %s\n' "$1"; }

# The pin has one source of truth: scripts/vendor-upstream.sh. Derive it rather than hardcoding
# it a second time, so changing the pin there cannot silently desync this check.
pinned_commit="$(grep '^UPSTREAM_COMMIT=' scripts/vendor-upstream.sh | sed -E 's/^UPSTREAM_COMMIT="([^"]*)"/\1/')"

if [[ -z "$pinned_commit" ]]; then
    fail "scripts/vendor-upstream.sh does not define UPSTREAM_COMMIT"
elif [[ ! -f docs/upstream.md ]]; then
    fail "docs/upstream.md is missing"
elif ! grep -q "$pinned_commit" docs/upstream.md; then
    fail "docs/upstream.md does not record the pinned commit $pinned_commit"
else
    pass "pinned commit recorded"
fi

# Files the build cannot do without, plus the machdbg-authored surface inside the vendored
# trees (see docs/upstream.md, "What was changed locally"). scripts/vendor-upstream.sh must
# preserve every one of these across a re-vendor; existence alone is what a destructive
# re-vendor would erase, so it is checked here even though it says nothing about content.
required=(
    src/cross/cmake.toml
    src/cross/widgets/CMakeLists.txt
    src/cross/widgets/Qt.cmake
    src/cross/ElfBug/ElfBug/api/elfbug_api.h
    src/cross/ElfBug/tests/TestHarness.h
    src/bridge/bridgemain.h
    src/dbg/_plugins.h
    cmake/cmkr.cmake
    src/cross/MachBug/MachBug/api/machbug_api.h
    src/cross/MachBug/MachBug/api/machbug_api.cpp
    src/cross/MachBug/tests/CMakeLists.txt
    src/cross/MachBug/tests/api_contract.cpp
    src/cross/MachBug/tests/api_contract_c.c
    src/cross/MachBug/tests/cmake.toml
    src/cross/MachBug/tests/targets_signed.cpp
    src/cross/MachBug/tests/targets.entitlements
    src/cross/MachBug/tests/targets/end_immediately.cpp
    src/cross/MachBug/tests/targets/exit_code_42.cpp
    src/cross/MachBug/tests/targets/hello_machbug.cpp
    src/cross/MachBug/tests/targets/run_endlessly.cpp
    src/cross/MachBug/tests/targets/multi_threaded.cpp
    src/cross/MachBug/tests/targets/crash_bad_access.cpp
    src/cross/MachBug/tests/mig_exception_server.cpp
    src/cross/MachBug/cmake/MachBugMig.cmake
    src/cross/MachBug/cmake/MachBugMigStubs.c
    src/cross/MachBug/tests/targets/no_get_task_allow.cpp
    src/cross/MachBug/tests/debugger_launch.cpp
    src/cross/MachBug/tests/exception_loop.cpp
    src/cross/MachBug/tests/TestHarness.h
    src/cross/MachBug/tests/test_harness.cpp
    src/cross/MachBug/tests/thread_resolution.cpp
    src/cross/MachBug/tests/registers_arm64.cpp
    src/cross/MachBug/MachBug/arch/Arm64.h
    src/cross/MachBug/MachBug/arch/Arm64.cpp
    src/cross/MachBug/MachBug/arch/Arch.h
    src/cross/MachBug/MachBug/arch/Arch.cpp
    src/cross/MachBug/MachBug/arch/X86_64.h
    src/cross/MachBug/MachBug/arch/X86_64.cpp
    src/cross/MachBug/tests/registers_x86_64.cpp
    src/cross/MachBug/tests/memory.cpp
    src/cross/MachBug/tests/register_format.cpp
    src/cross/MachBug/tests/stepping.cpp
    src/cross/MachBug/tests/breakpoints_software.cpp
    src/cross/MachBug/tests/targets/known_function.cpp
    src/cross/MachBug/MachBug/core/Breakpoints.h
    src/cross/MachBug/MachBug/core/Breakpoints.cpp
    src/cross/MachBug/MachBug/core/Threads.h
    src/cross/MachBug/MachBug/core/Threads.cpp
    src/cross/MachBug/tests/threads.cpp
    src/cross/views/cmake.toml
    src/cross/views/CMakeLists.txt
    src/cross/views/RegisterFormat.h
    src/cross/views/RegisterFormat.cpp
    src/cross/views/RegisterTable.h
    src/cross/views/RegisterTable.cpp
    src/cross/views/EngineMemoryPage.h
    src/cross/views/EngineMemoryPage.cpp
    src/cross/regview/main.cpp
    src/cross/regview/MainWindow.h
    src/cross/regview/MainWindow.cpp
    src/cross/tests/accessibility/regview_accessibility.py
    src/cross/MachBug/tests/targets/known_globals.cpp
    src/cross/MachBug/MachBug/memory/Memory.h
    src/cross/MachBug/MachBug/memory/Memory.cpp
    src/cross/MachBug/MachBug/cmake.toml
    src/cross/MachBug/MachBug/CMakeLists.txt
    src/cross/MachBug/MachBug/types/MachBug.h
    src/cross/MachBug/MachBug/types/Global.h
    src/cross/MachBug/MachBug/process/Process.h
    src/cross/MachBug/MachBug/process/Process.cpp
    src/cross/MachBug/MachBug/core/Debugger.h
    src/cross/MachBug/MachBug/core/Debugger.cpp
    src/cross/MachBug/MachBug/core/Debugger.Loop.cpp
    src/cross/MachBug/MachBug/core/ExceptionServer.cpp
    src/cross/CMakePresets.json
    src/cross/CMakeLists.txt
)
for path in "${required[@]}"; do
    if [[ -e "$path" ]]; then
        pass "$path"
    else
        fail "$path is missing"
    fi
done

# Existence is not enough for the three files shared with upstream: a re-vendor that overwrote
# them with upstream's version would leave them present but silently reverted. Assert the
# macOS-specific content a plain rm -rf + copy would destroy.
if [[ -f src/cross/cmake.toml ]] && grep -q 'MACHBUG_BUILD_TESTS' src/cross/cmake.toml \
    && grep -q 'machbug-tests' src/cross/cmake.toml \
    && grep -q '\[subdir\."MachBug/tests"\]' src/cross/cmake.toml; then
    pass "src/cross/cmake.toml carries the macOS MachBug conditions"
else
    fail "src/cross/cmake.toml is missing the macOS MachBug conditions (MACHBUG_BUILD_TESTS / machbug-tests / [subdir.\"MachBug/tests\"])"
fi

# Task 3 added the MachBug engine library itself, unconditionally on macOS (unlike the
# machbug-tests-gated tests subdir) -- a re-vendor that dropped just this one line would leave
# every file above present but the library no longer wired into the build.
if [[ -f src/cross/cmake.toml ]] && grep -q '\[subdir\."MachBug/MachBug"\]' src/cross/cmake.toml; then
    pass "src/cross/cmake.toml carries the MachBug library subdirectory"
else
    fail "src/cross/cmake.toml is missing [subdir.\"MachBug/MachBug\"]"
fi

if [[ -f src/cross/CMakeLists.txt ]] && grep -q 'MACHBUG_BUILD_TESTS' src/cross/CMakeLists.txt \
    && grep -q 'add_subdirectory("MachBug/tests")' src/cross/CMakeLists.txt; then
    pass "src/cross/CMakeLists.txt carries the generated MachBug/tests subdirectory"
else
    fail "src/cross/CMakeLists.txt is missing the generated MachBug/tests subdirectory (regenerate from cmake.toml with cmkr)"
fi

if [[ -f src/cross/CMakeLists.txt ]] \
    && grep -q 'add_subdirectory("MachBug/MachBug")' src/cross/CMakeLists.txt; then
    pass "src/cross/CMakeLists.txt carries the generated MachBug/MachBug subdirectory"
else
    fail "src/cross/CMakeLists.txt is missing the generated MachBug/MachBug subdirectory (regenerate from cmake.toml with cmkr)"
fi

if [[ -f cmake/cmkr.cmake ]] && grep -q 'vendor-and-strip fork (decision D3)' cmake/cmkr.cmake; then
    pass "cmake/cmkr.cmake carries the dead submodule bootstrap removal"
else
    fail "cmake/cmkr.cmake is missing the dead submodule bootstrap removal (decision D3)"
fi

# src/cross/widgets/Qt.cmake is in the same category: existence alone would still pass after a
# re-vendor silently reverted qt_executable to upstream's Windows-only WIN32 branch, dropping
# .app bundle support entirely. Unlike the three files above, this one is deliberately not in
# vendor-upstream.sh's AUTHORED_PATHS (see docs/upstream.md, "What was changed locally"), so
# nothing else preserves it automatically. The marker is the bundle identifier qt_executable
# sets on Apple: it names this fork specifically (upstream has no such string), and — unlike the
# surrounding set_target_properties block, which Tasks 3 and 4 extend with an icon and a
# macdeployqt step — it has no reason to change when they do.
if [[ -f src/cross/widgets/Qt.cmake ]] && grep -qF 'MACOSX_BUNDLE_GUI_IDENTIFIER "com.machdbg.${tgt}"' src/cross/widgets/Qt.cmake; then
    pass "src/cross/widgets/Qt.cmake carries the Apple MACOSX_BUNDLE branch"
else
    fail "src/cross/widgets/Qt.cmake is missing the Apple MACOSX_BUNDLE branch (qt_executable's com.machdbg.<target> MACOSX_BUNDLE_GUI_IDENTIFIER)"
fi

# Paths the strip script removes. They must not come back.
removed=(
    src/dbg/TitanEngine
    src/dbg/GleeBug
    src/dbg/XEDParse
    src/dbg/DeviceNameResolver
    src/exe
    src/launcher
    src/loaddll
)
for path in "${removed[@]}"; do
    if [[ -e "$path" ]]; then
        fail "$path should have been stripped"
    else
        pass "$path stripped"
    fi
done

# The safety net. Every file the widget library compiles must exist. This is what catches a
# strip that reaches too far.
#
# The pattern only matches paths ending in a source, header, ui or resource extension. The
# widgets_SOURCE_DIR variable is also used for bare directory references (an add_subdirectory
# call and two target_include_directories calls) that are not files at all; matching those too
# made this assertion fail unconditionally, on every tree, regardless of what was stripped.
widgets_cmake=src/cross/widgets/CMakeLists.txt
if [[ ! -f "$widgets_cmake" ]]; then
    fail "$widgets_cmake is missing"
else
    referenced=0
    while read -r rel; do
        referenced=$((referenced + 1))
        if [[ ! -f "src/gui/Src/$rel" ]]; then
            fail "widget library references missing file src/gui/Src/$rel"
        fi
    done < <(grep -oE 'widgets_SOURCE_DIR\}/[A-Za-z0-9_/.-]+\.(cpp|h|ui|qrc)' "$widgets_cmake" | sed 's|widgets_SOURCE_DIR}/||')
    # 78 real file references are present on the vendored tree; 75 leaves a little room for
    # upstream churn without tolerating a strip that reaches into src/gui/Src.
    if [[ "$referenced" -lt 75 ]]; then
        fail "expected at least 75 referenced widget sources, found $referenced"
    else
        pass "$referenced widget sources referenced and present"
    fi
fi

# A truncated or partial vendoring (an interrupted copy, a clone that ran out of disk) can still
# leave the handful of named files above in place and report GREEN. Guard against that with a
# floor on the total file count under src/ and cmake/. The healthy tree has roughly 1,318 files;
# 1,200 leaves room for this task's strip and for future upstream churn without tolerating a
# truncated copy.
file_count=$(find src cmake -type f | wc -l | tr -d ' ')
if [[ "$file_count" -lt 1200 ]]; then
    fail "expected at least 1200 files under src/ and cmake/, found $file_count"
else
    pass "$file_count files present under src/ and cmake/"
fi

exit "$failed"
