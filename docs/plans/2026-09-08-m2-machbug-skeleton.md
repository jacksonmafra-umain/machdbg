# Milestone 2 — MachBug Skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a process stop, resume, and report why — under machdbg's own control, through Mach exception ports, on Apple Silicon.

**Architecture:** `MachBug` mirrors `ElfBug`'s shape: a C++ `Debugger` class with virtual callbacks, an `api/machbug_api.cpp` that fills the `DbgEngine` vtable milestone 0 defined, and a recording harness that subclasses the debugger for tests. What does not transfer is the loop itself. `ElfBug` stops because `waitpid` returned; MachBug stops because a Mach message arrived and **has not yet been replied to**. The stopped state is a pending unanswered reply, and `Continue` is the act of replying.

**Tech Stack:** C++17, Mach APIs (`task_for_pid`, `task_set_exception_ports`, `mach_msg`, `thread_get_state`), MIG-generated `mach_exc_server` from the SDK's `mach_exc.defs`, `posix_spawn` with `POSIX_SPAWN_START_SUSPENDED`, `ptrace(PT_ATTACHEXC)`, Catch2 v3.14.0, cmkr.

**Spec:** `docs/specs/2026-09-07-macos-port-design.md` — sections 5 (the contract), 6 (MachBug internals) and 9 (signing) are the ones this milestone implements.

**Predecessors:** milestones 0 and 1, complete. The engine contract header exists at `src/cross/MachBug/MachBug/api/machbug_api.h` and its tests pass; nothing implements it yet.

## What was verified before this plan was written

Do not re-derive these; they are measured facts from this machine on 2026-09-08.

- **`mach_exc.defs` ships in the SDK** at `$(xcrun --show-sdk-path)/usr/include/mach/mach_exc.defs`, and `mig` is available through `xcrun -f mig`. This is the 64-bit-safe variant, which is the correct one for `MACH_EXCEPTION_CODES`.
- **`task_for_pid` does not need an Apple-issued identity, and does not need root**, for the case this milestone cares about. Measured, with an ad-hoc-signed caller carrying **no** debugger entitlement:

  | Target | Result |
  |---|---|
  | ad-hoc signed **with** `get-task-allow` | succeeds |
  | ad-hoc signed **without** `get-task-allow` | `KERN_FAILURE` (5) |
  | system binary with hardened runtime | `KERN_FAILURE` (5) |

  **This corrects the spec.** Section 9 states that `com.apple.security.cs.debugger` on an Apple-issued identity is required, and milestone 0 concluded CI would have to run engine tests as root. Neither holds for debugging targets we build ourselves and sign with `get-task-allow`. The entitlement is what buys you targets that did *not* opt in; it is not the price of entry. Task 1 records this in the spec.

## Global Constraints

- **The six verification scripts must stay green**: `check-toolchain.sh`, `verify-vendor.sh`, `check-credits.sh`, `check-bundles.sh`, `smoke-launch.sh`, `check-build-warnings.sh`. Run them before you commit. `check-bundles.sh` and `smoke-launch.sh` need a built tree.
- **Anything authored inside `src/` or `cmake/` must be added to `verify-vendor.sh`'s `required[]`** in the same commit, or a future re-vendor deletes it silently. This milestone adds a lot of code under `src/cross/MachBug/`.
- **CI must stay green.** `.github/workflows/macos.yml` runs on every push. If your work needs a new CI step, add it in the same pull request.
- **Configure and build from inside `src/cross`.** Presets are at `src/cross/CMakePresets.json`. Export `QT_ROOT_DIR="$(brew --prefix qt)"`. Engine work does not need Qt, but the shared configure does.
- **`MACHBUG_BUILD_TESTS` defaults to false**; the test target attaches to the `machbug-tests` condition. Configure with `-DMACHBUG_BUILD_TESTS=ON` to build them.
- **Mirror `ElfBug` where the shape transfers and diverge where it does not** — and say which you are doing. `src/cross/ElfBug/` is vendored, unbuilt, and present precisely as this reference.
- **A clean build takes about six minutes** plus Qt deployment. Do not rebuild to verify something a targeted build proves. Never end a turn waiting on a background build: run it inside your turn or do other work first.
- **Commits:** English, microcommits. **No assistant attribution of any kind** in commit messages or pull request bodies.
- **Workflow:** one GitHub issue per task assigned to `jacksonmafra-umain`, labelled, milestone `M2 MachBug skeleton`; one branch per task; one pull request per task **carrying labels, assignee and milestone**; nothing merged without the owner asking.
- **`rm -rf` is denied in this sandbox.** Use a fresh directory or Python's `shutil.rmtree`.
- **`#<issue>` in a pull request body** is the number `gh issue create` printed at the end of the
  URL in that same task's first step. Substitute it before running the command.
- **Starting a task:** `git checkout main && git pull --ff-only && git checkout -b <the branch
  named in that task's pull request step>`.

---

### Task 1: Test targets that can actually be debugged

Everything in this milestone is proven against small programs machdbg controls. `ElfBug/tests/targets/` has six; you need the same idea, signed so `task_for_pid` works.

**Files:**
- Create: `src/cross/MachBug/tests/targets/end_immediately.cpp`, `exit_code_42.cpp`, `hello_machbug.cpp`, `run_endlessly.cpp`, `multi_threaded.cpp`, `crash_bad_access.cpp`
- Create: `src/cross/MachBug/tests/targets.entitlements`
- Modify: `src/cross/MachBug/tests/cmake.toml`
- Modify: `scripts/verify-vendor.sh`
- Modify: `docs/specs/2026-09-07-macos-port-design.md`

**Interfaces:**
- Produces: fixture executables under `${CMAKE_BINARY_DIR}/tests/targets`, each ad-hoc signed with `get-task-allow`, and `MACHBUG_TESTS_TARGETS_DIR` pointing at them — mirroring `ELFBUG_TESTS_TARGETS_DIR`. Every later task's tests launch these.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Add signed test targets for the MachBug engine" \
  --assignee jacksonmafra-umain --milestone "M2 MachBug skeleton" \
  --label "type:feature,area:machbug,area:signing,prio:high" \
  --body "Every engine test in this milestone runs against a small program machdbg launches. ElfBug/tests/targets has six of them; MachBug needs the same, with one macOS-specific requirement: each must be ad-hoc signed with com.apple.security.get-task-allow, or task_for_pid fails and nothing else in the milestone can be tested.

This was measured rather than assumed. With an ad-hoc-signed caller carrying no debugger entitlement, task_for_pid succeeds against a target that has get-task-allow and fails with KERN_FAILURE against one that does not, and against hardened system binaries.

That corrects the design document: section 9 says an Apple-issued identity is required and milestone 0 concluded CI would need root. Neither is true for targets we build and sign ourselves. Record the corrected rule in the spec as part of this task."
```

- [ ] **Step 2: Write the failing test**

Create `src/cross/MachBug/tests/targets_signed.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
    // Reads the entitlements codesign reports for a file, or an empty string.
    std::string entitlementsOf(const std::string& path)
    {
        std::string command = "codesign --display --entitlements - \"" + path + "\" 2>&1";
        std::string output;
        if(FILE* pipe = popen(command.c_str(), "r"))
        {
            std::array<char, 512> buffer{};
            while(fgets(buffer.data(), int(buffer.size()), pipe) != nullptr)
                output += buffer.data();
            pclose(pipe);
        }
        return output;
    }
}

TEST_CASE("every test target is signed with get-task-allow")
{
    const std::string dir = MACHBUG_TESTS_TARGETS_DIR;
    for(const char* name : { "end_immediately", "exit_code_42", "hello_machbug",
                             "run_endlessly", "multi_threaded", "crash_bad_access" })
    {
        const std::string path = dir + "/" + name;
        INFO("target: " << path);
        REQUIRE(entitlementsOf(path).find("get-task-allow") != std::string::npos);
    }
}
```

- [ ] **Step 3: Run it to make sure it fails**

```bash
cd src/cross && cmake --preset macos-arm64 -DMACHBUG_BUILD_TESTS=ON >/dev/null \
  && cmake --build build/macos-arm64 --target MachBug_tests 2>&1 | tail -3
```

Expected: a compile failure — `MACHBUG_TESTS_TARGETS_DIR` is not defined and the targets do not exist. Capture it.

- [ ] **Step 4: Write the targets**

Six small programs. Keep them minimal; their value is being predictable.

```cpp
// end_immediately.cpp
int main() { return 0; }
```

```cpp
// exit_code_42.cpp
int main() { return 42; }
```

```cpp
// hello_machbug.cpp
#include <cstdio>
int main() { std::printf("hello machbug\n"); std::fflush(stdout); return 0; }
```

```cpp
// run_endlessly.cpp
#include <unistd.h>
int main() { for(;;) sleep(1); return 0; }
```

```cpp
// multi_threaded.cpp
#include <thread>
#include <chrono>
int main()
{
    std::thread a([]{ std::this_thread::sleep_for(std::chrono::seconds(30)); });
    std::thread b([]{ std::this_thread::sleep_for(std::chrono::seconds(30)); });
    a.join();
    b.join();
    return 0;
}
```

```cpp
// crash_bad_access.cpp
int main() { volatile int* p = nullptr; return *p; }
```

- [ ] **Step 5: Write the entitlements file**

Create `src/cross/MachBug/tests/targets.entitlements`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.get-task-allow</key>
    <true/>
</dict>
</plist>
```

- [ ] **Step 6: Build and sign them**

Extend `src/cross/MachBug/tests/cmake.toml`. Mirror `ElfBug/tests/cmake.toml`'s `elfbug_fixture` template, adding the signing step that macOS needs:

```toml
[template.machbug_fixture]
type = "executable"
compile-options = ["-g", "-O0"]
cmake-after = """
set_target_properties(${CMKR_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/targets"
)
add_custom_command(TARGET ${CMKR_TARGET} POST_BUILD
    COMMAND codesign --force --sign - --entitlements
            "${CMAKE_CURRENT_SOURCE_DIR}/targets.entitlements"
            "$<TARGET_FILE:${CMKR_TARGET}>"
    COMMENT "Signing ${CMKR_TARGET} with get-task-allow"
)
"""
```

Add one target per fixture using that template, and add
`compile-definitions = ['MACHBUG_TESTS_TARGETS_DIR="${CMAKE_BINARY_DIR}/tests/targets"']` to the
`MachBug_tests` target, mirroring `ELFBUG_TESTS_TARGETS_DIR`.

Regenerate with `cmkr gen`.

- [ ] **Step 7: Run the test to verify it passes**

```bash
cd src/cross && cmake --preset macos-arm64 -DMACHBUG_BUILD_TESTS=ON >/dev/null \
  && cmake --build build/macos-arm64 --target MachBug_tests >/dev/null \
  && ./build/macos-arm64/tests/MachBug_tests
```

Expected: PASS. Then confirm the premise by hand on one target:

```bash
codesign --display --entitlements - src/cross/build/macos-arm64/tests/targets/run_endlessly
```

- [ ] **Step 8: Prove `task_for_pid` actually works against them**

This is the step that de-risks the milestone. Launch `run_endlessly`, call `task_for_pid` against it from a small throwaway program, and confirm success. Then strip the entitlement from a copy and confirm failure. Capture both. If the first fails, **stop and report** — every later task depends on it.

- [ ] **Step 9: Guard the new files and correct the spec**

Add the new authored paths under `src/cross/MachBug/tests/` to `scripts/verify-vendor.sh`'s `required[]`.

In `docs/specs/2026-09-07-macos-port-design.md`, correct section 9: state the measured rule — `get-task-allow` on the target is what `task_for_pid` needs from a same-user caller; `com.apple.security.cs.debugger` is what buys targets that did not opt in. Correct section 11's claim that CI must run engine tests as root, since our own targets do not require it.

- [ ] **Step 10: Commit and open the pull request**

```bash
git add src/cross/MachBug/tests scripts/verify-vendor.sh docs/specs/2026-09-07-macos-port-design.md
git commit -m "Add signed test targets for the MachBug engine"
git push -u origin feat/machbug-test-targets
gh pr create --repo jacksonmafra-umain/machdbg --base main \
  --label "type:feature,area:machbug,area:signing,prio:high" \
  --assignee jacksonmafra-umain --milestone "M2 MachBug skeleton" \
  --title "Add signed test targets for the MachBug engine" \
  --body "Six fixture programs mirroring ElfBug/tests/targets, each ad-hoc signed with get-task-allow so task_for_pid can reach them.

The signing is not ceremony. Measured on this machine: an ad-hoc-signed caller with no debugger entitlement reaches a target that has get-task-allow, and gets KERN_FAILURE against one that does not. That corrects the design document, which claimed an Apple-issued identity was required and that CI would need root — neither is true for targets we build ourselves. Sections 9 and 11 now say so.

Closes #<issue>"
```

---

### Task 2: MIG-generated exception server

`mach_exc_server` is generated from `mach_exc.defs` by `mig`. Without it there is no exception loop, so this task exists on its own: it is small, it is pure build plumbing, and getting it wrong blocks everything after it.

**Files:**
- Create: `src/cross/MachBug/cmake/MachBugMig.cmake`
- Modify: `src/cross/MachBug/tests/cmake.toml` (or the MachBug library manifest, once it exists)
- Modify: `scripts/verify-vendor.sh`

**Interfaces:**
- Produces: generated `mach_excServer.c` and `mach_exc.h` in the build tree, and a CMake target other code links. Task 4 implements `catch_mach_exception_raise_state_identity` against it.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Generate the Mach exception server with MIG" \
  --assignee jacksonmafra-umain --milestone "M2 MachBug skeleton" \
  --label "type:feature,area:machbug,area:build,prio:high" \
  --body "The debug loop is a mach_msg receive loop served by mach_exc_server, which MIG generates from mach_exc.defs. That file ships in the SDK at \$(xcrun --show-sdk-path)/usr/include/mach/mach_exc.defs and mig is available through xcrun -f mig — both verified.

Use mach_exc.defs, not exc.defs: the mach_ variant carries 64-bit exception codes, which is what MACH_EXCEPTION_CODES requires and what any arm64 debugger needs.

Add a CMake rule generating the server into the build tree and a target other code can link. Nothing implements the callbacks yet; Task 4 does."
```

- [ ] **Step 2: Write the failing test**

Add to `src/cross/MachBug/tests/`, a compile-level assertion that the generated header exists and declares what we expect:

```cpp
#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <mach/mach.h>
// Generated by MIG from mach_exc.defs.
#include "mach_exc.h"
}

TEST_CASE("the MIG-generated exception server is available")
{
    // mach_exc_server is the dispatch routine the receive loop calls.
    REQUIRE(mach_exc_server != nullptr);
}
```

- [ ] **Step 3: Run it to make sure it fails**

Expected: `fatal error: 'mach_exc.h' file not found`. Capture it.

- [ ] **Step 4: Write the generation rule**

Create `src/cross/MachBug/cmake/MachBugMig.cmake`:

```cmake
# Generates the Mach exception server from the SDK's mach_exc.defs.
#
# mach_exc.defs rather than exc.defs: the mach_ variant carries 64-bit exception
# codes, which MACH_EXCEPTION_CODES requires and arm64 needs.
find_program(MACHBUG_MIG mig REQUIRED)

execute_process(
    COMMAND xcrun --show-sdk-path
    OUTPUT_VARIABLE MACHBUG_SDK_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(_defs "${MACHBUG_SDK_PATH}/usr/include/mach/mach_exc.defs")
if(NOT EXISTS "${_defs}")
    message(FATAL_ERROR "mach_exc.defs not found at ${_defs}")
endif()

set(MACHBUG_MIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/mig")
file(MAKE_DIRECTORY "${MACHBUG_MIG_DIR}")

add_custom_command(
    OUTPUT "${MACHBUG_MIG_DIR}/mach_excServer.c"
           "${MACHBUG_MIG_DIR}/mach_excUser.c"
           "${MACHBUG_MIG_DIR}/mach_exc.h"
    COMMAND "${MACHBUG_MIG}"
            -server "${MACHBUG_MIG_DIR}/mach_excServer.c"
            -user   "${MACHBUG_MIG_DIR}/mach_excUser.c"
            -header "${MACHBUG_MIG_DIR}/mach_exc.h"
            "${_defs}"
    DEPENDS "${_defs}"
    COMMENT "Generating the Mach exception server from mach_exc.defs"
    VERBATIM
)

add_library(machbug_mig STATIC
    "${MACHBUG_MIG_DIR}/mach_excServer.c"
    "${MACHBUG_MIG_DIR}/mach_excUser.c"
)
target_include_directories(machbug_mig PUBLIC "${MACHBUG_MIG_DIR}")
```

Wire it into the test target's manifest and link `machbug_mig`.

- [ ] **Step 5: Run the test to verify it passes**

Build and run. Expected: PASS. Then confirm the generated file really came from the 64-bit variant:

```bash
grep -c 'mach_exception_data_t\|MACH_EXCEPTION_CODES' src/cross/build/macos-arm64/**/mig/mach_exc.h
```

Report what you find rather than only that it compiled.

- [ ] **Step 6: Guard, commit, open the pull request**

Add the new authored file to `verify-vendor.sh`'s `required[]`. Branch `feat/machbug-mig`, base `main`, labels `type:feature,area:machbug,area:build,prio:high`.

---

### Task 3: Launch a process, suspended, and take its task port

The first real engine behaviour. No exceptions yet — just start a process under our control and hold the handle that makes everything else possible.

**Files:**
- Create: `src/cross/MachBug/MachBug/types/MachBug.h`, `types/Global.h`
- Create: `src/cross/MachBug/MachBug/process/Process.h`, `process/Process.cpp`
- Create: `src/cross/MachBug/MachBug/core/Debugger.h`, `core/Debugger.cpp`
- Create: `src/cross/MachBug/MachBug/cmake.toml`
- Modify: `src/cross/cmake.toml`, `scripts/verify-vendor.sh`

**Interfaces:**
- Produces: `MachBug::Debugger` with `Init(const char* path, const char* const* argv = nullptr)`,
  `GetPid()`, `GetTaskPort()` returning `mach_port_t`, and `Terminate()` for a child that was
  launched but never started; `MachBug::Process` owning the pid and the port. Task 4 adds the
  exception loop to this class; Task 6 wraps it in the C vtable.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Launch a suspended process and acquire its task port" \
  --assignee jacksonmafra-umain --milestone "M2 MachBug skeleton" \
  --label "type:feature,area:machbug,prio:high" \
  --body "The first engine behaviour: MachBug::Debugger::Init launches a target with posix_spawn and POSIX_SPAWN_START_SUSPENDED, then calls task_for_pid to take its task port.

Suspended start is deliberate — it gives a clean window to install exception ports before the target executes an instruction, which Task 4 needs.

Mirror ElfBug's class shape: a Debugger class with virtual callbacks in core/, a Process owning the handle in process/, shared typedefs in types/. Diverge where the platform demands it and say so.

Done when a test launches run_endlessly, obtains a valid task port, and terminates it cleanly."
```

- [ ] **Step 2: Write the failing test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <MachBug/core/Debugger.h>

#include <string>

#define FIXTURE(name) (std::string(MACHBUG_TESTS_TARGETS_DIR "/") + (name))

TEST_CASE("a suspended launch yields a valid task port")
{
    MachBug::Debugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    REQUIRE(debugger.GetPid() > 0);
    REQUIRE(debugger.GetTaskPort() != MACH_PORT_NULL);
    // Start() is not running here, so there is no loop to Stop(); kill the suspended child.
    debugger.Terminate();
}

TEST_CASE("launching a path that does not exist fails cleanly")
{
    MachBug::Debugger debugger;
    REQUIRE_FALSE(debugger.Init("/nonexistent/machdbg-no-such-binary"));
    REQUIRE(debugger.GetPid() == 0);
}
```

- [ ] **Step 3: Run it to make sure it fails**

Expected: `fatal error: 'MachBug/core/Debugger.h' file not found`.

- [ ] **Step 4: Implement the launch**

The sequence, and the reasons, since each step has one:

1. `posix_spawnattr_init`, then `posix_spawnattr_setflags` with `POSIX_SPAWN_START_SUSPENDED`. The child exists but has executed nothing.
2. `posix_spawn` with the target path and argv.
3. `task_for_pid(mach_task_self(), pid, &task)`. On failure, report the `kern_return_t` translated to a named diagnostic — a denial here is the single most common failure in the product and must never surface as "unknown error".
4. Keep the child suspended. Task 4 resumes it after installing exception ports.

Mirror `ElfBug`'s file layout. Where `ElfBug::Process` holds a pid, `MachBug::Process` holds a pid **and** a `mach_port_t`; say so in a comment, because that is the structural difference between the two engines.

- [ ] **Step 5: Run the test to verify it passes**

Build and run. Expected: PASS, both cases.

- [ ] **Step 6: Confirm the child really is suspended**

Launch `hello_machbug` — which prints — and confirm nothing is printed before you resume or kill it. A suspended start that is not actually suspended would produce a race Task 4 could not diagnose.

- [ ] **Step 7: Guard, commit, open the pull request**

Branch `feat/machbug-launch`, base `main`, labels `type:feature,area:machbug,prio:high`.

---

### Task 4: The exception loop

The heart of the milestone, and the part with no `ElfBug` counterpart. A Mach exception arrives as a message; the target stays stopped until the message is answered.

**Files:**
- Create: `src/cross/MachBug/MachBug/core/Debugger.Loop.cpp`
- Create: `src/cross/MachBug/MachBug/core/ExceptionServer.cpp`
- Modify: `core/Debugger.h`, `core/Debugger.cpp`
- Modify: `src/cross/MachBug/tests/`, `scripts/verify-vendor.sh`

**Interfaces:**
- Produces: `Start` (blocking, owns the loop) and thread-safe `Continue`, `StepInto`, `Pause`, `Stop`, matching the semantics `machbug_api.h` documents. Task 6 exposes these through the vtable.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Implement the Mach exception loop" \
  --assignee jacksonmafra-umain --milestone "M2 MachBug skeleton" \
  --label "type:feature,area:machbug,prio:high" \
  --body "Install a Mach exception port on the target task, receive exceptions with mach_msg, and hold the reply until the user resumes.

This is where MachBug stops mirroring ElfBug. ElfBug stops because waitpid returned; MachBug stops because a message arrived and has not been answered. The stopped state is the pending reply, and Continue is the act of replying KERN_SUCCESS. The waitpid loop shape does not transfer, and trying to make it transfer is the most likely way to get this wrong.

Start blocks and owns the loop. Continue, StepInto, Pause and Stop are called from another thread and reach the loop through a queue guarded by a mutex and condition variable, matching the thread-safety split machbug_api.h documents and ElfBug already follows.

Done when a test launches a target, receives its first exception, observes the process stopped, resumes it, and sees it run to completion."
```

- [ ] **Step 2: Write the failing test**

```cpp
TEST_CASE("the first exception stops the target until it is resumed")
{
    MachBug::test::RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("hello_machbug").c_str()));
    debugger.StartOnThread();

    REQUIRE(debugger.WaitFor(MachBug::test::EventType::SystemBreakpoint));
    REQUIRE(debugger.IsStopped());

    debugger.Continue();
    REQUIRE(debugger.WaitFor(MachBug::test::EventType::ExitProcess));
    REQUIRE(debugger.LastExitCode() == 0);
}
```

The `RecordingDebugger` harness is Task 5's deliverable; write this test now, let it fail to compile, and bring it green when Task 5 lands — or write a minimal inline recorder here and let Task 5 replace it. Say which you chose.

- [ ] **Step 3: Run it to make sure it fails**

- [ ] **Step 4: Install the exception ports**

```cpp
mach_port_t exceptionPort = MACH_PORT_NULL;
mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &exceptionPort);
mach_port_insert_right(mach_task_self(), exceptionPort, exceptionPort,
                       MACH_MSG_TYPE_MAKE_SEND);

task_set_exception_ports(
    task,
    EXC_MASK_BREAKPOINT | EXC_MASK_BAD_ACCESS | EXC_MASK_BAD_INSTRUCTION
        | EXC_MASK_ARITHMETIC | EXC_MASK_SOFTWARE,
    exceptionPort,
    EXCEPTION_STATE_IDENTITY | MACH_EXCEPTION_CODES,
    ARM_THREAD_STATE64);   // NOT THREAD_STATE_NONE -- see below
```

`MACH_EXCEPTION_CODES` is what makes the codes 64-bit and is why Task 2 used `mach_exc.defs`. `EXCEPTION_STATE_IDENTITY` gives both the thread port and the thread state in one message, which is what a debugger wants.

**The last argument is not optional, and getting it wrong fails silently.** An earlier version of this plan specified `THREAD_STATE_NONE`, which is valid only for `EXCEPTION_DEFAULT`. With a state-carrying behaviour and no flavor, the kernel declines to deliver, says nothing, and the exception falls through to the default handler, which converts it to a signal. The observable result is that `task_set_exception_ports` returns `KERN_SUCCESS`, the target faults for real, and `mach_msg` simply times out — which looks exactly like an environment that forbids exception ports, and cost this milestone a wrong BLOCKED and a CI probe before the constant was suspected.

Measured, in standalone C outside the codebase: with `THREAD_STATE_NONE`, `mach_msg` times out after five seconds and the child dies of `SIGSEGV`. With `ARM_THREAD_STATE64`, `mach_msg` returns success with message id 2407 — `mach_exception_raise_state_identity` — and the child is alive and stopped, held by the unanswered reply.

Milestone 3 brings x86-64 into scope and will need `x86_THREAD_STATE64`, so select the flavor by architecture rather than hardcoding it.

**`task_set_exception_ports` alone is not enough for a target that does not fault.** A process that runs normally never raises an exception, so nothing ever stops. `ptrace(PT_ATTACHEXC, pid, 0, 0)` is what makes the resuming signal itself arrive as an exception on your port — which is how the debugger gets its first stop on a well-behaved program. Install the ports before attaching, so the attach's own stop cannot race a port that does not exist yet.

**A linkage trap sits between this task and Task 2.** The MIG server needs a `catch_mach_exception_raise_state_identity` symbol. If a weak fallback exists in one static archive and your strong definition in another, lazy archive extraction can leave the weak one winning with no diagnostic at all — a debugger that builds, links, runs, and returns `KERN_FAILURE` for every exception. Prefer deleting the weak fallback so a missing strong handler is an undefined-symbol error at build time.

Verify the ports are installed **before** resuming the child from its suspended start.

- [ ] **Step 5: Write the receive loop and the handler**

The loop is `mach_msg` receive, then `mach_exc_server(&request.Head, &reply.Head)`, then `mach_msg` send of the reply — **except** that the reply is deliberately withheld while the target is stopped.

MIG calls a function you provide. Its name and signature are fixed:

```c
extern "C" kern_return_t catch_mach_exception_raise_state_identity(
    mach_port_t exception_port,
    mach_port_t thread,
    mach_port_t task,
    exception_type_t exception,
    mach_exception_data_t code,
    mach_msg_type_number_t codeCnt,
    int* flavor,
    thread_state_t old_state,
    mach_msg_type_number_t old_stateCnt,
    thread_state_t new_state,
    mach_msg_type_number_t* new_stateCnt);
```

MIG also requires `catch_mach_exception_raise` and `catch_mach_exception_raise_state` to exist even though this behaviour never calls them; provide them returning `KERN_FAILURE` and say so in a comment.

**The design point that matters:** returning from the handler is what sends the reply and resumes the target. So the handler must not return while the user expects the target to be stopped. Block inside it on the command queue until `Continue`, `StepInto` or `Stop` arrives, then return the value that expresses the decision. Document this clearly — it is the single least obvious thing in the engine.

- [ ] **Step 6: Implement the thread-safe commands**

`Continue`, `StepInto`, `Pause`, `Stop` post to a queue guarded by a mutex and condition variable; the handler waits on it. `Pause` is `task_suspend`, not a signal.

- [ ] **Step 7: Bring the test green, and prove the stop is real**

Beyond the test passing, demonstrate that the target is genuinely stopped rather than merely slow: while stopped, confirm `hello_machbug` has not printed, wait a second, confirm it still has not, then resume and see the output appear. A stop nobody proved is a race waiting to be discovered later.

- [ ] **Step 8: Guard, commit, open the pull request**

Branch `feat/machbug-exception-loop`, base `main`.

---

### Task 5: The recording test harness

`ElfBug/tests/TestHarness.h` subclasses the debugger and records an event timeline. MachBug needs the same, and every later milestone's tests will use it.

**Files:**
- Create: `src/cross/MachBug/tests/TestHarness.h`
- Modify: `src/cross/MachBug/tests/`, `scripts/verify-vendor.sh`

**Interfaces:**
- Produces: `MachBug::test::RecordingDebugger` with `StartOnThread()`, `WaitFor(EventType)`
  returning false on timeout, `IsStopped()`, `LastExitCode()`, and the event timeline itself.
  Task 4's test calls `IsStopped()` and `LastExitCode()`, so those two names are load-bearing —
  if you rename them, fix that test in the same commit. Milestone 3 onward extends this harness.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Add the MachBug recording test harness" \
  --assignee jacksonmafra-umain --milestone "M2 MachBug skeleton" \
  --label "type:feature,area:machbug,prio:high" \
  --body "Mirror ElfBug/tests/TestHarness.h: a RecordingDebugger subclassing MachBug::Debugger, overriding the virtual callbacks to append to a timestamped event timeline, with StartOnThread and a WaitFor that blocks with a timeout.

Read the ElfBug harness first and follow its shape. Its EventType set is the right starting point; add the events MachBug has and ElfBug does not, and say which those are.

Every engine test from here to milestone 9 uses this, so its ergonomics matter more than its line count. A test that has to sleep to be reliable is a harness failure, not a test failure."
```

- [ ] **Step 2 through 6**

Read `src/cross/ElfBug/tests/TestHarness.h` in full. Mirror it: the `EventType` enum, the `Event` struct with a `steady_clock` timestamp, the `RecordingDebugger` class, `StartOnThread`, and a `WaitFor` that blocks on a condition variable with a timeout and returns false rather than hanging.

**Do not use sleeps for synchronisation.** If a test needs a sleep to pass reliably, the harness is missing a wait primitive; add the primitive.

Write a test for the harness itself: launch `exit_code_42`, run to completion, and assert the timeline contains `CreateProcess` followed by `ExitProcess` with code 42, in that order, with the timestamps ordered.

Then retro-fit Task 4's exception test onto the harness if it used a temporary recorder.

Branch `feat/machbug-test-harness`, base `main`.

---

### Task 6: The C vtable and attach

Fill the `DbgEngine` vtable milestone 0 defined, and add attaching to an already-running process.

**Files:**
- Create: `src/cross/MachBug/MachBug/api/machbug_api.cpp`
- Modify: `core/Debugger.h`, `core/Debugger.cpp`
- Modify: `src/cross/MachBug/tests/api_contract.cpp`, `scripts/verify-vendor.sh`

**Interfaces:**
- Produces: `MachBugCreate` returning a `DbgEngine*` whose `Start`, `Continue`, `StepInto`, `Pause`, `Stop`, `GetPid` and `GetArch` are implemented; the memory, register, module and breakpoint entries remain null and are milestone 3 and 4's work. **Say so explicitly in the report** — a vtable with silent null entries is a crash waiting to happen, so decide and document whether unimplemented entries are null or stubs returning `DbgStatus_NotSupported`.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Implement the DbgEngine vtable and process attach" \
  --assignee jacksonmafra-umain --milestone "M2 MachBug skeleton" \
  --label "type:feature,area:machbug,prio:high" \
  --body "Bridge MachBug::Debugger to the C contract milestone 0 defined, the way ElfBug's api/elfbug_api.cpp bridges its own class, and add Attach for a process that is already running.

Attach uses ptrace(PT_ATTACHEXC) plus task_for_pid, not PT_ATTACH — the EXC variant routes exceptions to the Mach port rather than to signals, which is the whole model this engine is built on.

Milestone 2 implements the lifecycle entries only. Memory, registers, modules and breakpoints belong to milestones 3 and 4. Decide explicitly whether the unimplemented entries are null or stubs returning DbgStatus_NotSupported, and document the choice: a caller reaching a null function pointer crashes, which is a poor way to learn a feature is not ready."
```

- [ ] **Step 2: Write the failing test**

Extend `api_contract.cpp` — which today only proves the vtable is fillable — with a test that fills it for real:

```cpp
TEST_CASE("the engine launches and runs a target through the C contract")
{
    DbgEngineCallbacks callbacks{};
    // Record exit through userdata; keep it minimal, the harness covers the rest.
    DbgEngine* engine = MachBugCreate(&callbacks);
    REQUIRE(engine != nullptr);

    DbgLaunchSpec spec{};
    spec.path = MACHBUG_TESTS_TARGETS_DIR "/exit_code_42";

    REQUIRE(engine->Start(engine->impl, &spec) == DbgStatus_Ok);
    REQUIRE(engine->GetPid(engine->impl) > 0);

    MachBugDestroy(engine);
}
```

Add a second test attaching to a separately spawned `run_endlessly` via `spec.attachPid`.

- [ ] **Steps 3 to 7**

Implement, bring green, prove attach works against a process machdbg did not launch, confirm the six scripts, commit, open the pull request.

Branch `feat/machbug-vtable`, base `main`.

---

### Task 7: CI and signing documentation

The engine tests need signed targets and must run in CI. Milestone 0 assumed they would need root; Task 1 established they do not.

**Files:**
- Modify: `.github/workflows/macos.yml`
- Modify: `docs/COMPILE-macos.md`

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Run the MachBug engine tests in CI" \
  --assignee jacksonmafra-umain --milestone "M2 MachBug skeleton" \
  --label "type:chore,area:build,area:machbug,prio:high" \
  --body "Add a CI step building and running MachBug_tests with -DMACHBUG_BUILD_TESTS=ON.

Milestone 0's plan assumed engine tests would have to run as root because task_for_pid needs an entitlement. Task 1 measured otherwise: a same-user caller reaches a target signed with get-task-allow without any entitlement or elevation. The CI step therefore runs as the ordinary runner user, and the reason should be stated in the workflow so nobody adds sudo back.

Document in docs/COMPILE-macos.md how to run the engine tests locally and why the targets are signed."
```

- [ ] **Steps 2 to 5**

Add the step, push, watch the run, fix what it reveals. **Do not remove a step to make CI pass.** If the engine tests cannot run on a GitHub runner — a plausible outcome worth discovering — report exactly how they fail rather than skipping them, and we will decide together.

Record in the workflow, next to the step, why it does not use `sudo`.

Branch `ci/machbug-engine-tests`, base `main`.

---

## Milestone 2 exit criteria

On a clean checkout:

```bash
export QT_ROOT_DIR="$(brew --prefix qt)"
./scripts/check-toolchain.sh && ./scripts/verify-vendor.sh && ./scripts/check-credits.sh
cd src/cross
cmake --preset macos-arm64 -DMACHBUG_BUILD_TESTS=ON
cmake --build build/macos-arm64 --target MachBug_tests
./build/macos-arm64/tests/MachBug_tests
```

All tests pass, and specifically:

- A target launches suspended and its task port is acquired without root and without an Apple-issued identity.
- The first Mach exception stops the target, the stop is demonstrably real, and resuming runs it to completion.
- Attaching to a process machdbg did not launch works.
- `Continue`, `StepInto`, `Pause` and `Stop` are callable from another thread while `Start` owns the loop.
- The engine tests run in CI as the ordinary user, green.

What milestone 2 deliberately does not deliver: reading or writing memory, reading or writing registers, breakpoints of any kind, module enumeration, and anything x86-64. Those are milestones 3 and 4, and `macos-universal` remains unbuildable until the official Qt is installed.
