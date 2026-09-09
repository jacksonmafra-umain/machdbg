# Milestone 4 — Breakpoints and Stepping Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop a target where the user asked — at an instruction, or on a read or write of an address — on arm64 and x86-64, and step it one instruction at a time.

**Architecture:** One breakpoint table in the engine (`core/Breakpoints`), one dispatch that turns a Mach exception into "which breakpoint was this", and everything architecture-shaped pushed down into `arch/Arm64` and `arch/X86_64`, which milestone 3 created: the software trap's bytes and alignment, the program-counter fixup after it, the debug-register encoding for hardware breakpoints and watchpoints, and single-step. Above the C API nothing learns any of it — a breakpoint is a kind, an address and a size (spec section 5). The one genuinely new mechanism is thread tracking: macOS debug state is per-thread, so a hardware breakpoint has to be reapplied to every thread the target creates, and the engine has to notice those threads at all.

**Tech Stack:** C++17, `thread_get_state`/`thread_set_state` with `ARM_DEBUG_STATE64` and `x86_DEBUG_STATE64`, `task_threads`, `mach_vm_protect`/`mach_vm_write` (milestone 3's `MachBug::memory`), `sysctl hw.optional.breakpoint`/`watchpoint`, Qt 6 with the vendored `StdTable`, Catch2 v3.14.0, cmkr.

**Spec:** `docs/specs/2026-09-07-macos-port-design.md` — section 5 (breakpoints and callbacks), section 6 (stepping and breakpoints, pointer authentication) and section 12's milestone 4 row.

**Predecessors:** milestones 0 through 3, complete and merged. `MachBug::arch` reads and writes general registers on both architectures, `MachBug::memory` writes into code pages (flipping and restoring protection), the vtable's register and memory entries work, and `regview.app` shows a live target's registers and memory.

## Decisions taken before this plan was written

Settled with the project owner on 2026-09-09. Do not reopen them mid-task.

1. **Thread tracking comes into this milestone**, not milestone 5. `task_threads` plus `onThreadCreate`/`onThreadExit`, and the engine reapplies debug state to every new thread. Spec section 6 names this as the reason those callbacks exist; without it a hardware breakpoint works by accident — it fires on the thread that existed when it was armed and silently misses every other.
2. **All four kinds ship in this milestone**: `SOFTWARE`, `HW_EXEC`, `HW_READ`, `HW_WRITE`, with each architecture's size and alignment rules.
3. **`regview` gets a full breakpoint bench**: a list with enable/disable, the four kinds selectable, and a watchpoint hit showing the data address that triggered it.
4. **Running out of hardware slots is a named failure, not a downgrade.** `SetBreakpoint` refuses and says how many slots exist and how many are in use. `GetHwBreakpointSlots()` is in the API so a UI can know the limit before asking; silently installing a software breakpoint instead would change the target's observable bytes, which self-modifying code and code checksums can see — and a watchpoint has no software equivalent to fall back to at all.

## What was verified before this plan was written

Measured on this machine (Apple Silicon, macOS 26.x, SDK 26.5) on 2026-09-09. Do not re-derive.

- **`ARM_DEBUG_STATE64` is flavor `15`.** `arm_debug_state64_t` is `__bvr[16]`, `__bcr[16]`, `__wvr[16]`, `__wcr[16]`, `__mdscr_el1` — breakpoint value/control pairs, watchpoint value/control pairs, and the single-step bit.
- **`x86_DEBUG_STATE64` is flavor `11`** (`mach/i386/thread_status.h:113`).
- **The arrays hold 16 entries, and this machine has 6 breakpoint and 4 watchpoint slots**: `sysctl hw.optional.breakpoint` → `6`, `hw.optional.watchpoint` → `4`. **Do not report 16 from the array size.** `GetHwBreakpointSlots()` must read the sysctl, because the struct is sized for the widest implementation and using anything beyond the real count silently does nothing.
- **Single-step already exists** and works on both architectures, in an anonymous namespace in `core/Debugger.Loop.cpp:100-136`: `mdscr_el1` bit 0 on arm64, `rflags` bit 8 (TF) on x86-64. Task 1 moves it, it does not invent it.
- **Writing into a target's code works and restores protection**, measured in milestone 3: a write into a read-execute page fails with `KERN_INVALID_ADDRESS` (not `KERN_PROTECTION_FAILURE`), and `MachBug::memory::Write` already flips with `VM_PROT_COPY`, writes and restores. Software breakpoints build on that rather than reimplementing it.
- **The exception loop currently classifies exactly two things** (`Debugger.Loop.cpp:483-515`): a signal passthrough (`EXC_SOFTWARE` with `code[0] == EXC_SOFT_SIGNAL`) and "the first stop", with everything else going to `cbException`. There is no breakpoint dispatch to extend — Task 3 adds it.
- **`arm_thread_state64_t.__x` holds 29 entries**; `x29`/`x30` are `__fp`/`__lr`. Milestone 3's `arch/Arm64.cpp` already handles that, and any new code reading registers goes through `MachBug::arch::Read` rather than repeating it.

**Left to measure, by the task that needs it, rather than assumed here:** the exact `code[0]`/`code[1]` an arm64 hardware breakpoint and an arm64 watchpoint arrive with (Task 5 Step 2 and Task 6 Step 2), and whether a `BRK #0` leaves the program counter on the trapping instruction while `0xCC` leaves it one byte past (Task 2 Step 6). Each of those tasks measures it first and writes down what it saw.

## A note on this plan's resolution

Tasks 1 to 4 are written at the granularity the previous milestone's plan used: every step, with
the test and the code. Tasks 5 to 9 name their files, interfaces, tests and decisions, but
compress their middle steps into a described sequence rather than spelling out each one.

That is deliberate and it is a trade, not an oversight. What tasks 5 to 9 turn on is not code
anyone can write down in advance: it is four measurements — the exception encoding an arm64
hardware breakpoint arrives with, the same for a watchpoint, the program counter a watchpoint hit
reports, and the slot counts each architecture admits to. Each of those tasks therefore *starts*
by measuring, and the code that follows is written against what was seen. Spelling out the
implementation here would mean inventing the very facts those steps exist to establish, and a
plan that guesses them reads as authoritative while being wrong.

If a task's shape turns out to be wrong once measured, stop and say so rather than bending the
measurement to fit this document.

## Global Constraints

- **The six verification scripts must stay green**: `check-toolchain.sh`, `verify-vendor.sh`, `check-credits.sh`, `check-bundles.sh`, `smoke-launch.sh`, `check-build-warnings.sh`.
- **Anything authored inside `src/` or `cmake/` must be added to `verify-vendor.sh`'s `required[]` in the same commit**, or a future re-vendor deletes it silently.
- **New CMake source files need `cmkr gen` in their own directory** before they build: the committed `CMakeLists.txt` files are generated from `cmake.toml`, and a glob in the manifest does not pick up a new file on its own. Forgetting this produces an undefined symbol at link time, not a configure error.
- **CI must stay green, on both architectures.** `.github/workflows/macos.yml` runs the whole engine suite on `macos-15` and on `macos-15-intel`; the Intel job is no longer `continue-on-error`, so an x86-64 regression fails the build. A new sample-app target must be added to the workflow's build step as well as to `check-bundles.sh` and `smoke-launch.sh` — a bundle that is checked but never built fails as "has no executable".
- **Configure and build from inside `src/cross`.** Presets: `macos-arm64`, `macos-x86_64`, `macos-universal`. Export `QT_ROOT_DIR="$(brew --prefix qt)"`. Engine tests need `-DMACHBUG_BUILD_TESTS=ON`.
- **`kern_return_t` never crosses the C API.** One translation point per entry, `DbgLastErrorString()` for the detail, and the status vocabulary milestone 3 established: `NotAttached` for no target, `InvalidArgument` for a request that cannot name what it asks for, `Failed` for one the kernel refused.
- **No architecture check above the C contract.** If a widget or app needs `if (arch == ...)`, something is missing from `MachBug::arch` — add it there.
- **Never leave a target modified.** A test that patches code restores it; the engine removes every software breakpoint's bytes on `Stop()` and on `DeleteBreakpoint`. A milestone that leaves `0xCC` in a target's text is a milestone that corrupts whatever runs next.
- **A clean build takes about six minutes** plus Qt deployment. Use targeted builds while iterating.
- **Commits:** English, microcommits. **No assistant attribution of any kind** in commit messages or pull request bodies.
- **Workflow:** one GitHub issue per task assigned to `jacksonmafra-umain`, labelled, milestone `M4 Breakpoints and stepping`; one branch per task **cut from `main` after pulling** (a branch accidentally cut from the previous task's branch carries its commits into the pull request and fails CI on checks that belong elsewhere — that happened twice in milestone 3); one pull request per task carrying labels, assignee and milestone.
- **Labels that exist**: `arch:arm64`, `arch:x86_64`, `area:machbug`, `area:widgets`, `area:build`, `area:memory`, `type:feature`, `type:chore`, `type:bug`, `prio:high`, `prio:medium`, `prio:low`. There is no `area:gui` — `gh` refuses the whole command on an unknown label.
- **Create the milestone once, before Task 1:** `gh api repos/jacksonmafra-umain/machdbg/milestones -f title="M4 Breakpoints and stepping" -f description="Software and hardware breakpoints, watchpoints and single-step, both architectures"` — it fails harmlessly if it already exists.
- **`rm -rf` is denied in this sandbox.** Use a fresh directory or Python's `shutil.rmtree`.
- **`#<issue>` in a pull request body** is the number `gh issue create` printed in that same task's first step.

---

### Task 1: Single-step, in the arch layer where it belongs

`setSingleStep` works today but lives in an anonymous namespace in `core/Debugger.Loop.cpp`, invisible to anything else — including the breakpoint code in Task 3, which needs it for the restore-step-re-arm cycle. Moving it is the whole task, plus the test the move makes possible.

**Files:**
- Modify: `src/cross/MachBug/MachBug/arch/Arm64.h`, `arch/Arm64.cpp`, `arch/X86_64.h`, `arch/X86_64.cpp`, `arch/Arch.h`, `arch/Arch.cpp`
- Modify: `src/cross/MachBug/MachBug/core/Debugger.Loop.cpp`
- Test: `src/cross/MachBug/tests/stepping.cpp` (create)
- Modify: `src/cross/MachBug/tests/cmake.toml`, `scripts/verify-vendor.sh`

**Interfaces:**
- Produces: `bool MachBug::arch::SetSingleStep(DbgArch arch, mach_port_t thread, bool enable, std::string* error);` and the per-arch `Arm64::SetSingleStep`/`X86_64::SetSingleStep` behind it. Task 3 calls the dispatch.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Move single-step into the arch layer" \
  --assignee jacksonmafra-umain --milestone "M4 Breakpoints and stepping" \
  --label "type:chore,area:machbug,prio:high" \
  --body "setSingleStep is implemented for both architectures in an anonymous namespace in core/Debugger.Loop.cpp, where nothing else can reach it. Milestone 4's restore-step-re-arm cycle needs it from the breakpoint code, and spec section 6 describes it as two mechanisms behind one entry -- which is what arch/ is for.

Move it to arch/Arm64 and arch/X86_64 with the dispatch in MachBug::arch, no behaviour change, and add the test the move makes possible: stepping a stopped target advances its program counter and does not run it to completion."
```

- [ ] **Step 2: Write the failing test**

Create `src/cross/MachBug/tests/stepping.cpp`:

```cpp
// Milestone 4 task 1: single-step, now reachable from outside the exception loop.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include <MachBug/arch/Arch.h>
#include <MachBug/core/Debugger.h>

#include "TestHarness.h"

#define FIXTURE(name) (std::string(MACHBUG_TESTS_TARGETS_DIR "/") + (name))

using MachBug::test::EventType;
using MachBug::test::RecordingDebugger;

namespace
{
#if defined(__arm64__) || defined(__aarch64__)
    constexpr DbgArch kHostArch = DbgArch_Arm64;
#else
    constexpr DbgArch kHostArch = DbgArch_X86_64;
#endif

    uint64_t programCounterOf(const DbgRegisters& regs)
    {
#if defined(__arm64__) || defined(__aarch64__)
        return regs.arm64.pc;
#else
        return regs.x86_64.rip;
#endif
    }
}

TEST_CASE("a step advances the program counter and leaves the target stopped")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    DbgRegisters before{};
    std::string error;
    REQUIRE(MachBug::arch::Read(kHostArch, debugger.ResolveThread(0), &before, &error));

    debugger.StepInto();
    REQUIRE(debugger.WaitFor(EventType::Step));
    REQUIRE(debugger.IsStopped());

    DbgRegisters after{};
    REQUIRE(MachBug::arch::Read(kHostArch, debugger.ResolveThread(0), &after, &error));
    INFO("pc was 0x" << std::hex << programCounterOf(before) << ", is now 0x"
         << programCounterOf(after));
    REQUIRE(programCounterOf(after) != programCounterOf(before));

    // One instruction, not a resume: run_endlessly never exits, so an ExitProcess here would
    // mean the step let it run freely.
    REQUIRE(debugger.count(EventType::ExitProcess) == 0);
}

TEST_CASE("single-step is reachable through the arch dispatch")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    const mach_port_t thread = debugger.ResolveThread(0);
    std::string error;
    REQUIRE(MachBug::arch::SetSingleStep(kHostArch, thread, true, &error));
    INFO("diagnostic: " << error);
    REQUIRE(MachBug::arch::SetSingleStep(kHostArch, thread, false, &error));

    // A thread that does not exist is refused with a diagnostic rather than reported as armed.
    REQUIRE_FALSE(MachBug::arch::SetSingleStep(kHostArch, MACH_PORT_NULL, true, &error));
    REQUIRE_FALSE(error.empty());
}
```

- [ ] **Step 3: Run it to make sure it fails**

```bash
cd src/cross && cmake --preset macos-arm64 -DMACHBUG_BUILD_TESTS=ON >/dev/null \
  && cmake --build build/macos-arm64 --target MachBug_tests 2>&1 | grep -E "error" | head -3
```

Expected: `no member named 'SetSingleStep' in namespace 'MachBug::arch'`. Add the file to `tests/cmake.toml`'s sources and run `cmkr gen` in `src/cross/MachBug/tests` first, or the test is not compiled at all and the run passes vacuously.

- [ ] **Step 4: Declare it in the arch headers**

In `arch/Arm64.h` and `arch/X86_64.h`, next to `Read`/`Write`:

```cpp
    // Arms or disarms hardware single-step for one thread. On arm64 that is MDSCR_EL1 bit 0; on
    // x86-64 it is the TF bit in rflags. Two mechanisms, one entry (spec section 6) -- and both
    // are per-thread, like every other piece of debug state on this platform.
    bool SetSingleStep(mach_port_t thread, bool enable, std::string* error);
```

In `arch/Arch.h`:

```cpp
    bool SetSingleStep(DbgArch arch, mach_port_t thread, bool enable, std::string* error);
```

- [ ] **Step 5: Move the implementations**

Move the two `setSingleStep` bodies out of `core/Debugger.Loop.cpp`'s anonymous namespace into `arch/Arm64.cpp` and `arch/X86_64.cpp`, inside each file's existing `#if` for its own architecture, and give them the diagnostics the rest of those files have:

```cpp
    bool SetSingleStep(const mach_port_t thread, const bool enable, std::string* error)
    {
        arm_debug_state64_t state{};
        mach_msg_type_number_t count = ARM_DEBUG_STATE64_COUNT;
        if(thread == MACH_PORT_NULL)
        {
            if(error)
                *error = "no thread to single-step: the target is not stopped, or the thread id "
                         "names no thread of it";
            return false;
        }
        // ARM_DEBUG_STATE64 is flavor 15; the struct is bvr/bcr/wvr/wcr plus mdscr_el1, and this
        // function touches only the last of those -- Task 5 and Task 6 own the others, and they
        // read-modify-write the same state, so neither may clobber this bit.
        const kern_return_t got = thread_get_state(thread, ARM_DEBUG_STATE64,
            reinterpret_cast<thread_state_t>(&state), &count);
        if(got != KERN_SUCCESS)
        {
            if(error)
                *error = std::string("thread_get_state(ARM_DEBUG_STATE64) failed: ") +
                         mach_error_string(got);
            return false;
        }

        if(enable)
            state.__mdscr_el1 |= 1ULL;  // MDSCR_EL1 bit 0: SS
        else
            state.__mdscr_el1 &= ~1ULL;

        const kern_return_t set = thread_set_state(thread, ARM_DEBUG_STATE64,
            reinterpret_cast<thread_state_t>(&state), ARM_DEBUG_STATE64_COUNT);
        if(set != KERN_SUCCESS)
        {
            if(error)
                *error = std::string("thread_set_state(ARM_DEBUG_STATE64) failed: ") +
                         mach_error_string(set);
            return false;
        }
        return true;
    }
```

The x86-64 twin is the same shape over `x86_thread_state64_t.__rflags` and `0x100`, with its own diagnostics. Both files' `#else` branches get the refusing version the register functions already use ("this build cannot ... it is not an arm64 build").

`Arch.cpp` dispatches exactly as `Read`/`Write` do, including the `default:` that refuses an architecture nothing implements.

- [ ] **Step 6: Call the dispatch from the loop**

In `core/Debugger.Loop.cpp`, replace the two call sites of the removed helper with:

```cpp
            std::string stepError;
            if(MachBug::arch::SetSingleStep(currentArch(), thread, true, &stepError))
            {
                mStepArmed = true;
            }
            else
            {
                cbInternalError(stepError);
            }
```

`currentArch()` is a small file-local helper returning the host's `DbgArch` — the loop has no engine-level architecture query of its own, and adding one is Task 7's business (the vtable's `GetArch`). Write it as a static function with a comment saying so.

- [ ] **Step 7: Build, run the suite, and check nothing regressed**

```bash
cd src/cross && cmake --build build/macos-arm64 --target MachBug_tests 2>&1 | tail -2 \
  && ./build/macos-arm64/tests/MachBug_tests 2>&1 | tail -3
```

Expected: all tests pass, two more cases than before, and the existing `StepInto` test in `exception_loop.cpp` still passes — it is the regression check that the move changed no behaviour.

- [ ] **Step 8: Guard the new file, commit, open the pull request**

```bash
git add -A && git commit -m "Move single-step into the arch layer"
git push -u origin chore/machbug-arch-single-step
gh pr create --base main --title "Move single-step into the arch layer" \
  --assignee jacksonmafra-umain --milestone "M4 Breakpoints and stepping" \
  --label "type:chore,area:machbug,prio:high" \
  --body "Closes #<issue>.

setSingleStep worked but lived in an anonymous namespace in the exception loop, where the breakpoint code that needs it cannot reach it. No behaviour change: the same two mechanisms, now behind MachBug::arch::SetSingleStep with the diagnostics the rest of that layer carries.

Test plan: two new cases -- a step advances the program counter and leaves the target stopped (not resumed: run_endlessly never exits, so an ExitProcess would mean the step let it run), and the dispatch refuses a thread that does not exist. The existing StepInto test is the regression check on the move itself."
```

---

### Task 2: Software breakpoints

**Files:**
- Create: `src/cross/MachBug/MachBug/core/Breakpoints.h`, `core/Breakpoints.cpp`
- Modify: `src/cross/MachBug/MachBug/arch/Arm64.h`, `arch/Arm64.cpp`, `arch/X86_64.h`, `arch/X86_64.cpp`, `arch/Arch.h`, `arch/Arch.cpp`
- Create: `src/cross/MachBug/tests/targets/known_function.cpp`
- Test: `src/cross/MachBug/tests/breakpoints_software.cpp` (create)
- Modify: `src/cross/MachBug/tests/cmake.toml`, `src/cross/MachBug/MachBug/cmake.toml`, `scripts/verify-vendor.sh`

**Interfaces:**
- Consumes: `MachBug::memory::{Read,Write}` (milestone 3), `MachBug::arch::SetSingleStep` (Task 1).
- Produces:
  - `MachBug::arch::SoftwareTrap(DbgArch)` → `struct Trap { const uint8_t* bytes; uint32_t size; uint32_t alignment; }` — `0xCC`/1/1 on x86-64, `BRK #0`/4/4 on arm64.
  - `class MachBug::Breakpoints` with `Add`, `Remove`, `SetEnabled`, `Find(address)`, `All()`, `RestoreAll`, and `ArmAll` — the table Task 3's dispatch consults and Task 7's vtable drives.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Install and remove software breakpoints" \
  --assignee jacksonmafra-umain --milestone "M4 Breakpoints and stepping" \
  --label "type:feature,area:machbug,prio:high" \
  --body "0xCC on x86-64, a 4-byte BRK #0 at a 4-byte-aligned address on arm64, with the original bytes saved so removal is exact. Milestone 3's MachBug::memory::Write already flips page protection with VM_PROT_COPY and restores it, so this builds on that rather than reimplementing the W^X dance.

The table lives in the engine and is guarded: SetBreakpoint arrives from a UI thread while the loop thread services exceptions.

An arm64 address that is not 4-byte aligned is refused with a named diagnostic rather than patched -- a BRK written across an instruction boundary corrupts two instructions and traps at neither.

Nothing may be left behind in the target: every installed byte is restored on removal and on Stop()."
```

- [ ] **Step 2: Write the fixture**

Create `src/cross/MachBug/tests/targets/known_function.cpp`:

```cpp
// Publishes the address of a function it then calls forever, so a breakpoint test knows an
// address that is (a) real code, (b) reached repeatedly, and (c) not inferred from a register.
#include <cstdint>
#include <cstdio>
#include <unistd.h>

// noinline and not static: the address published below has to be the address actually called,
// and an inlined or folded function has neither.
__attribute__((noinline)) void machbug_breakpoint_target(void)
{
    // A volatile write, so the body cannot be optimised away to nothing and the function keeps a
    // real instruction to trap on.
    static volatile uint64_t counter = 0;
    counter = counter + 1;
}

int main()
{
    std::printf("%p\n", (void*)&machbug_breakpoint_target);
    std::fflush(stdout);
    for(;;)
    {
        machbug_breakpoint_target();
        usleep(50 * 1000);
    }
    return 0;
}
```

Register it in `src/cross/MachBug/tests/cmake.toml` exactly as `known_globals` is (the `machbug_fixture` template, an `OUTPUT_NAME`, and an entry in `add_dependencies(MachBug_tests ...)`), so it is signed with `get-task-allow` at build time.

- [ ] **Step 3: Write the failing test**

Create `src/cross/MachBug/tests/breakpoints_software.cpp`. It reuses the capture technique `memory.cpp` uses to read a fixture's printed address (redirect stdout for `Init()`, restore, poll the file), then:

```cpp
TEST_CASE("a software breakpoint stops the target at the address it was set on")
{
    // <launch known_function with the stdout capture, exactly as memory.cpp does, into
    //  `functionAddress`; the fixture prints it as %p, so "%llx" reads it back>
    RecordingDebugger debugger;
    // ... Init, StartOnThread, WaitFor(SystemBreakpoint), Continue, WaitFor(Resumed),
    //     poll the capture file, Pause()

    MachBug::Breakpoints breakpoints;
    std::string error;
    REQUIRE(breakpoints.Add(debugger.GetTaskPort(), kHostArch, functionAddress,
                            DbgBreakpointKind_Software, 0, &error));
    INFO("diagnostic: " << error);

    // The byte at the address is now the trap, not what the target shipped.
    uint8_t installed[4]{};
    REQUIRE(MachBug::memory::Read(debugger.GetTaskPort(), functionAddress, installed,
                                  sizeof(installed), &error));
    const MachBug::arch::Trap trap = MachBug::arch::SoftwareTrap(kHostArch);
    REQUIRE(std::memcmp(installed, trap.bytes, trap.size) == 0);

    debugger.Continue();
    // The fixture calls the function every 50ms, so the trap fires without any further nudging.
    REQUIRE(debugger.WaitFor(EventType::Exception));

    DbgRegisters regs{};
    REQUIRE(MachBug::arch::Read(kHostArch, debugger.ResolveThread(0), &regs, &error));
    INFO("stopped with pc at 0x" << std::hex << programCounterOf(regs) << ", expected 0x"
         << functionAddress);
    // Task 3 makes this exact -- the program-counter fixup after the trap is its work. Here the
    // assertion is deliberately weaker: the stop happened inside the function that was patched.
    REQUIRE(programCounterOf(regs) >= functionAddress);
    REQUIRE(programCounterOf(regs) <= functionAddress + 16);

    // Removal is exact: the original bytes come back.
    REQUIRE(breakpoints.Remove(debugger.GetTaskPort(), kHostArch, functionAddress, &error));
    uint8_t restored[4]{};
    REQUIRE(MachBug::memory::Read(debugger.GetTaskPort(), functionAddress, restored,
                                  sizeof(restored), &error));
    REQUIRE(std::memcmp(restored, installed, trap.size) != 0);
}

TEST_CASE("an unaligned arm64 software breakpoint is refused")
{
#if defined(__arm64__) || defined(__aarch64__)
    // ... launch run_endlessly and stop it
    MachBug::Breakpoints breakpoints;
    std::string error;
    DbgRegisters regs{};
    REQUIRE(MachBug::arch::Read(kHostArch, debugger.ResolveThread(0), &regs, &error));

    // One byte past a real instruction: writing a 4-byte BRK there would corrupt two
    // instructions and trap at neither.
    REQUIRE_FALSE(breakpoints.Add(debugger.GetTaskPort(), kHostArch,
                                  programCounterOf(regs) + 1, DbgBreakpointKind_Software, 0,
                                  &error));
    INFO("diagnostic: " << error);
    REQUIRE(error.find("align") != std::string::npos);
#endif
}

TEST_CASE("the table restores every byte it wrote")
{
    // ... launch known_function, capture the address, stop
    MachBug::Breakpoints breakpoints;
    std::string error;
    uint8_t original[4]{};
    REQUIRE(MachBug::memory::Read(debugger.GetTaskPort(), functionAddress, original,
                                  sizeof(original), &error));

    REQUIRE(breakpoints.Add(debugger.GetTaskPort(), kHostArch, functionAddress,
                            DbgBreakpointKind_Software, 0, &error));
    REQUIRE(breakpoints.RestoreAll(debugger.GetTaskPort(), kHostArch, &error));

    uint8_t after[4]{};
    REQUIRE(MachBug::memory::Read(debugger.GetTaskPort(), functionAddress, after,
                                  sizeof(after), &error));
    REQUIRE(std::memcmp(original, after, sizeof(original)) == 0);

    // And the table knows it holds nothing armed, so a later ArmAll cannot double-save bytes
    // that are already the trap.
    REQUIRE(breakpoints.All().size() == 1);
    REQUIRE_FALSE(breakpoints.All().front().armed);
}
```

Write the launch-and-capture preamble out in full in each case rather than sharing a helper across files; if it appears three times in this one file, factor it into a file-local function there.

- [ ] **Step 4: Run it to make sure it fails**

Expected: `'MachBug/core/Breakpoints.h' file not found`.

- [ ] **Step 5: Add the trap description to the arch layer**

In `arch/Arch.h`:

```cpp
    // The software trap for an architecture: its bytes, its size, and the alignment an address
    // must satisfy to hold it. arm64's BRK #0 is 0xD4200000, which must sit on a 4-byte boundary
    // because every arm64 instruction does; x86-64's INT3 is one byte and needs no alignment.
    struct Trap
    {
        const uint8_t* bytes;
        uint32_t size;
        uint32_t alignment;
    };

    // Returns a zero-size trap for an architecture this engine has none for, so a caller cannot
    // patch a target with an empty or borrowed encoding.
    Trap SoftwareTrap(DbgArch arch);
```

`arch/Arm64.cpp` holds `static const uint8_t kBrk0[4] = {0x00, 0x00, 0x20, 0xD4};` — little-endian `0xD4200000` — and `arch/X86_64.cpp` holds `static const uint8_t kInt3[1] = {0xCC};`. **Verify the arm64 encoding before trusting it**: `echo 'brk #0' | as -arch arm64 -o /tmp/brk.o - && otool -t /tmp/brk.o` prints the bytes; record what it printed in the pull request.

- [ ] **Step 6: Measure the program counter after a trap**

Before writing Task 3's dispatch, establish what the two architectures do. With the test from Step 3 stopped at the trap, print `pc` and compare it to the breakpoint address:

```bash
cd src/cross && ./build/macos-arm64/tests/MachBug_tests "a software breakpoint stops*" -s 2>&1 | grep -A3 "stopped with pc"
```

Record the delta. On x86-64 the `0xCC` has already executed, so `rip` is one byte past the address; on arm64 the `BRK` traps without retiring, so `pc` is on the instruction. **Whatever you measure goes into a comment on `arch::PcFixupAfterTrap()` in Task 3, and into the pull request.** Do not carry the received wisdom into the code without checking it.

- [ ] **Step 7: Write `Breakpoints.h`**

```cpp
#pragma once

#include <mach/mach.h>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <MachBug/api/machbug_api.h>

namespace MachBug
{
    // The engine's breakpoint table: what the user asked for, what was written into the target to
    // achieve it, and enough to undo it exactly.
    //
    // Guarded, unlike most of this engine: SetBreakpoint() arrives on whatever thread the UI runs
    // on while the loop thread is servicing exceptions and consulting the same table to classify
    // them. The mutex is this class's own -- it deliberately does not reuse Debugger's command
    // mutex, which exists to hand a decision to a parked handleException() and would deadlock if
    // a breakpoint query took it while that decision was pending.
    class Breakpoints
    {
    public:
        struct Entry
        {
            uint64_t address = 0;
            DbgBreakpointKind kind = DbgBreakpointKind_Software;
            uint32_t size = 0;          // watchpoints only; 0 for the others
            bool enabled = true;        // what the user asked for
            bool armed = false;         // whether the target currently carries it
            int slot = -1;              // hardware only; -1 while unassigned
            std::vector<uint8_t> original;  // software only: the bytes the target shipped
        };

        // Adds and arms. Refuses a duplicate address, an unaligned software breakpoint, a
        // watchpoint with a size the architecture cannot encode, and -- once Task 5 lands -- a
        // hardware request with no free slot, each with its own diagnostic.
        bool Add(mach_port_t task, DbgArch arch, uint64_t address, DbgBreakpointKind kind,
                 uint32_t size, std::string* error);

        // Disarms and forgets. Restoring the original bytes is part of removal, not a separate
        // step a caller can skip.
        bool Remove(mach_port_t task, DbgArch arch, uint64_t address, std::string* error);

        // Arms or disarms in the target without forgetting the entry, which is what a UI's
        // enable/disable checkbox means.
        bool SetEnabled(mach_port_t task, DbgArch arch, uint64_t address, bool enabled,
                        std::string* error);

        // The entry an exception at `address` belongs to, or nullopt. Task 3's dispatch.
        std::optional<Entry> Find(uint64_t address) const;

        std::vector<Entry> All() const;

        // Takes every armed breakpoint out of the target without forgetting them -- the first
        // half of the restore-step-re-arm cycle, and what Stop() owes the target.
        bool RestoreAll(mach_port_t task, DbgArch arch, std::string* error);

        // Puts every enabled breakpoint back. The second half of that cycle.
        bool ArmAll(mach_port_t task, DbgArch arch, std::string* error);

    private:
        mutable std::mutex mMutex;
        std::vector<Entry> mEntries;
    };
}
```

- [ ] **Step 8: Write `Breakpoints.cpp` for the software case only**

Hardware slots are Task 5's; here `Add` refuses anything that is not `DbgBreakpointKind_Software` with `"hardware breakpoints are not implemented yet"` — a named refusal, not a silent no-op, and a line Task 5 deletes. Arming is: read `trap.size` bytes into `Entry::original`, then write the trap; disarming is writing `original` back. Both go through `MachBug::memory`, so the protection dance and its diagnostics come for free.

Every refusal names the address in hex and says what was wrong with it.

- [ ] **Step 9: Wire into the build, run the suite, run the gates, commit, open the pull request**

Branch `feat/machbug-software-breakpoints`. The pull request body carries the `as`-verified `BRK #0` bytes and the program-counter delta measured in Step 6.

---

### Task 3: Stopping at a breakpoint, and continuing past it

The table exists; nothing consults it. This task is the dispatch and the restore-step-re-arm cycle — the part that makes a breakpoint usable rather than merely installed.

**Files:**
- Modify: `src/cross/MachBug/MachBug/core/Debugger.h`, `core/Debugger.Loop.cpp`
- Modify: `src/cross/MachBug/MachBug/arch/Arch.h`, `arch/Arch.cpp`, `arch/Arm64.cpp`, `arch/X86_64.cpp`
- Test: `src/cross/MachBug/tests/breakpoints_software.cpp` (extend)
- Modify: `src/cross/MachBug/tests/TestHarness.h`

**Interfaces:**
- Consumes: `Breakpoints::{Find,RestoreAll,ArmAll}` (Task 2), `arch::SetSingleStep` (Task 1).
- Produces: `Debugger::Breakpoints()` (the table, for Task 7's vtable), `cbBreakpoint(uint64_t address)` fired for a hit, `arch::PcFixupAfterTrap(DbgArch)`, and `EventType::Breakpoint` in the harness.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Stop at a software breakpoint and continue past it" \
  --assignee jacksonmafra-umain --milestone "M4 Breakpoints and stepping" \
  --label "type:feature,area:machbug,prio:high" \
  --body "The exception loop classifies a signal passthrough and 'the first stop' and sends everything else to cbException. A breakpoint hit has to become its own thing: identify the address, apply the architecture's program-counter fixup, and report cbBreakpoint.

Continuing from a software breakpoint is the restore-step-re-arm cycle spec section 6 describes: the trap byte has to come out, the target has to advance one instruction, and the trap has to go back -- otherwise Continue() either re-traps forever at the same address or loses the breakpoint.

cbBreakpoint is one of the eight callbacks from the original brief and has been unfired since milestone 2. This is the task that gives it something to report."
```

- [ ] **Step 2: Extend the harness**

`TestHarness.h` gains `EventType::Breakpoint` and the override that pushes it, with the address in `Event::address` — the field `cbException` already uses. Update the header comment that enumerates which of ElfBug's event types this harness has and why: `Breakpoint` stops being a gap and becomes a slot, and the comment should say what changed rather than leaving a stale list.

- [ ] **Step 3: Write the failing tests**

Added to `breakpoints_software.cpp`:

```cpp
TEST_CASE("a breakpoint hit is reported as a breakpoint, at the address it was set on")
{
    // ... launch known_function, capture functionAddress, stop, add a software breakpoint
    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::Breakpoint));

    const auto events = debugger.events();
    const auto hit = std::find_if(events.begin(), events.end(),
        [](const MachBug::test::Event& e) { return e.type == EventType::Breakpoint; });
    REQUIRE(hit != events.end());
    INFO("reported address 0x" << std::hex << hit->address << ", expected 0x" << functionAddress);
    REQUIRE(hit->address == functionAddress);

    // The program counter is on the breakpoint's instruction, not one byte past it: a caller that
    // resumes from here must re-execute what the trap replaced.
    DbgRegisters regs{};
    std::string error;
    REQUIRE(MachBug::arch::Read(kHostArch, debugger.ResolveThread(0), &regs, &error));
    REQUIRE(programCounterOf(regs) == functionAddress);
}

TEST_CASE("continuing past a breakpoint runs the real instruction and keeps the breakpoint")
{
    // ... same setup, stop at the hit
    debugger.Continue();

    // The fixture calls the function in a loop, so a breakpoint that survived the resume fires
    // again -- and one that was lost never does. WaitFor consumes up to its match, so this is
    // the *second* hit, not the first one seen again.
    REQUIRE(debugger.WaitFor(EventType::Breakpoint));
    REQUIRE(debugger.count(EventType::Breakpoint) >= 2);

    // And the target is still running its own code, not stuck re-trapping: the function's
    // counter is what proves the replaced instruction actually executed.
    REQUIRE(debugger.count(EventType::InternalError) == 0);
}

TEST_CASE("a disabled breakpoint stops nothing")
{
    // ... launch known_function, add a breakpoint, then disable it before resuming
    std::string error;
    REQUIRE(debugger.Breakpoints().SetEnabled(debugger.GetTaskPort(), kHostArch,
                                              functionAddress, false, &error));
    debugger.Continue();

    // A generous dwell rather than a wait: the assertion is that nothing arrives, and the
    // fixture calls the function every 50ms, so 500ms is ten chances to trap.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    REQUIRE(debugger.count(EventType::Breakpoint) == 0);
}
```

- [ ] **Step 4: Run them to make sure they fail**

Expected: `no member named 'Breakpoint' in 'MachBug::test::EventType'` first; after Step 2, the hit tests fail because `cbBreakpoint` is never fired and `WaitFor` times out.

- [ ] **Step 5: Add the program-counter fixup to the arch layer**

```cpp
// In arch/Arch.h
    // How far the program counter has moved past a software trap by the time the exception
    // arrives, in bytes. See the implementation for what was measured on each architecture.
    uint32_t PcFixupAfterTrap(DbgArch arch);
```

Arm64 returns `0` and x86-64 returns `1`, each with a comment recording Task 2 Step 6's measurement — including, if it differed from this, what was actually seen.

- [ ] **Step 6: Dispatch in `handleException()`**

Insert before the existing first-stop/step classification, and after the signal-passthrough branch:

```cpp
        // A breakpoint the user asked for, identified by address rather than by exception code:
        // both architectures report a software trap as EXC_BREAKPOINT, and the code that
        // accompanies it says which *kind* of debug event it was, not which of our breakpoints.
        // The table is the only thing that knows that.
        const uint32_t fixup = arch::PcFixupAfterTrap(currentArch());
        DbgRegisters regs{};
        std::string regError;
        if(exception == EXC_BREAKPOINT && !wasStepCompletion &&
           arch::Read(currentArch(), thread, &regs, &regError))
        {
            const uint64_t pc = programCounter(regs) - fixup;
            if(const auto entry = mBreakpoints.Find(pc); entry && entry->armed)
            {
                // Rewound before reporting, so the caller sees the address it asked for and a
                // resume re-executes the instruction the trap replaced. On x86-64 this is a real
                // write; on arm64 the fixup is zero and this is a no-op.
                if(fixup != 0)
                    arch::SetProgramCounter(currentArch(), thread, pc, &regError);

                mStoppedAtBreakpoint = pc;
                park(...);           // the same command-queue park the first stop uses
                cbBreakpoint(pc);
                ...
            }
        }
```

Write it as a helper on `Debugger` rather than inline in the middle of `handleException()` if it makes that function harder to follow than it already is; the function's own comment says it is the trickiest in the engine, and this task should not make that worse.

`arch::SetProgramCounter` is new here — one line per architecture over the existing `Write` path (`"pc"`/`"rip"` are already in the descriptor tables, so it can be `Write(arch, thread, "pc", value, error)` and needs no new arch entry at all; prefer that and say so).

- [ ] **Step 7: The restore-step-re-arm cycle**

When the decision from the command queue is `Continue` and `mStoppedAtBreakpoint` is set:

1. `mBreakpoints.RestoreAll(...)` — or, better, restore only the one at that address; restoring all of them is simpler and correct but stops every other breakpoint from firing during the step, which for a single instruction is acceptable. **Choose one, and say why in a comment.**
2. `arch::SetSingleStep(..., true, ...)`, set `mSteppingOverBreakpoint = true`.
3. Return from `handleException()`, which sends the reply and lets the target execute exactly one instruction.
4. On the next exception (the step completion), re-arm with `ArmAll`, clear `mSteppingOverBreakpoint`, disarm single-step, and — unless the user asked for a step — resume without reporting anything to the caller. That last part matters: the step is machinery, not something the user asked for, and firing `cbStep` for it would make every `Continue` look like a step.

- [ ] **Step 8: Build, run the suite three times, commit, open the pull request**

Three runs, not one: this task adds the milestone's only multi-stop timing-sensitive path, and a re-arm that races the target's next call would show up as an occasional missed second hit rather than a consistent failure.

Branch `feat/machbug-breakpoint-dispatch`.

---

### Task 4: Thread tracking

**Files:**
- Create: `src/cross/MachBug/MachBug/core/Threads.h`, `core/Threads.cpp`
- Modify: `src/cross/MachBug/MachBug/core/Debugger.h`, `core/Debugger.Loop.cpp`
- Modify: `src/cross/MachBug/MachBug/api/machbug_api.cpp`
- Test: `src/cross/MachBug/tests/threads.cpp` (create)
- Modify: `src/cross/MachBug/tests/TestHarness.h`, `tests/cmake.toml`, `src/cross/MachBug/MachBug/cmake.toml`, `scripts/verify-vendor.sh`

**Interfaces:**
- Produces: `MachBug::Threads` with `std::vector<mach_port_t> Snapshot(mach_port_t task)` and `Diff` returning appeared/disappeared sets; `Debugger` fires `cbThreadCreate(uint64_t threadId)` / `cbThreadExit(uint64_t threadId)`; the vtable forwards both. Task 5 uses the appeared set to arm new threads.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Notice the threads a target creates and destroys" \
  --assignee jacksonmafra-umain --milestone "M4 Breakpoints and stepping" \
  --label "type:feature,area:machbug,prio:high" \
  --body "macOS debug state is per-thread, so a hardware breakpoint has to be applied to every thread of the target -- including the ones created after it was set. The engine currently has no idea a target has more than one thread: ResolveThread validates a port against task_threads and that is all.

Diff task_threads against the known set at every stop, fire onThreadCreate/onThreadExit, and expose the appeared set so task 5 can arm new threads before they run.

At every stop rather than on a notification, deliberately: a new thread raises no exception on the port this engine owns, and a stop is the only moment the engine can safely touch another thread's state. Say so in the code -- the polling is a consequence of the platform, not a shortcut.

onThreadCreate and onThreadExit are two of the four callbacks section 5 says macOS forces into the API; they have been unfired since milestone 2."
```

- [ ] **Step 2: Write the failing test**

`multi_threaded.cpp` already exists as a fixture. The test launches it, stops it, and asserts the engine reports more than one thread and fires the callbacks:

```cpp
TEST_CASE("the engine notices the threads a target creates")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("multi_threaded").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    // At the first stop the target has not run its own code yet, so its extra threads do not
    // exist -- one thread, and no ThreadCreate for it: the thread that raised the first stop was
    // there before the engine was looking, and reporting it as "created" would be a fiction.
    REQUIRE(debugger.ThreadCount() == 1);
    REQUIRE(debugger.count(EventType::ThreadCreate) == 0);

    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::Resumed));

    // Pausing is what gives the engine its next look at the thread list.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    debugger.Pause();
    REQUIRE(debugger.IsStopped());
    REQUIRE(debugger.ThreadCount() > 1);
    REQUIRE(debugger.count(EventType::ThreadCreate) >= 1);
}
```

Read `tests/targets/multi_threaded.cpp` first and, if it exits too quickly for this shape, extend it to park after spawning its threads — noting in its comment that the breakpoint tests need it alive.

- [ ] **Step 3 to Step 7: implement, wire the callbacks, run, commit, open the pull request**

`Threads::Snapshot` releases every port `task_threads` hands over, as `ResolveThread` already does — a leaked thread port keeps a dead thread's state alive and eventually exhausts the port table. The diff is over the previous snapshot; the first snapshot after `Init()`/`Attach()` establishes the baseline without reporting anything, which is what the test's `ThreadCreate == 0` assertion pins down.

Branch `feat/machbug-thread-tracking`.

---

### Task 5: Hardware execution breakpoints

**Files:**
- Modify: `arch/Arm64.h`, `arch/Arm64.cpp`, `arch/X86_64.h`, `arch/X86_64.cpp`, `arch/Arch.h`, `arch/Arch.cpp`
- Modify: `core/Breakpoints.h`, `core/Breakpoints.cpp`, `core/Debugger.Loop.cpp`
- Test: `src/cross/MachBug/tests/breakpoints_hardware.cpp` (create)
- Modify: `tests/cmake.toml`, `scripts/verify-vendor.sh`

**Interfaces:**
- Consumes: `Threads` (Task 4), the breakpoint table (Task 2), the dispatch (Task 3).
- Produces: `arch::SlotCounts(DbgArch)` → `{ uint32_t exec; uint32_t watch; }`, `arch::ApplyDebugState(DbgArch, thread, const DebugSlots&, error)`, `arch::DecodeDebugTrap(...)`, and `Breakpoints::Add` accepting `DbgBreakpointKind_HwExec`.

- [ ] **Step 1: Open the issue** — the body states the measured slot counts (6 exec, 4 watch on this machine, from `sysctl hw.optional.breakpoint`/`watchpoint`), that the arrays hold 16 but only the sysctl count is real, and that exhaustion is a named failure rather than a downgrade to software.

- [ ] **Step 2: Measure what a hardware trap looks like**

Arm one BVR/BCR pair on a stopped target's thread at a known function address, resume, and print the exception's `exception`, `codeCnt`, `code[0]` and `code[1]` from a temporary `cbException` override. **Record all four in the pull request**, and write the classification in Task 5's dispatch against what you saw — not against what a manual says. Arm64's `EXC_BREAKPOINT` carries a subcode that distinguishes an instruction breakpoint from a watchpoint, and getting it wrong makes a watchpoint hit report as an unrelated exception.

- [ ] **Step 3 to Step 9** — the failing test (a hardware breakpoint at the fixture's function address stops the target without modifying its bytes: `MemRead` at the address still returns the original instruction, which is the property software breakpoints cannot offer and the reason hardware ones exist); slot allocation in the table; `ApplyDebugState` per architecture; propagation to threads that appear (using Task 4's appeared set); the exhaustion test (`SlotCounts().exec + 1` requests, the last refused with a diagnostic naming both numbers); and the propagation test against `multi_threaded`, which is the one that proves Task 4 was worth pulling in.

Branch `feat/machbug-hardware-breakpoints`.

---

### Task 6: Watchpoints

**Files:** as Task 5, plus `src/cross/MachBug/tests/targets/writes_a_global.cpp` and `tests/watchpoints.cpp`.

**Interfaces:**
- Produces: `Breakpoints::Add` accepting `DbgBreakpointKind_HwRead` and `DbgBreakpointKind_HwWrite` with a size, and `cbBreakpoint` reporting the **data** address for a watchpoint hit.

- [ ] **Step 1: Open the issue** — stating that the size and alignment rules differ (arm64 encodes a byte-address-select mask in `WCR`; x86-64 encodes length in `DR7`'s LEN bits and requires the address to be aligned to the length), and that a size the architecture cannot encode is refused rather than rounded.

- [ ] **Step 2: Write the fixture** — `writes_a_global.cpp` prints a global's address and then writes an incrementing value to it in a loop, flushing nothing: the write is the event under test, not the print.

- [ ] **Step 3: Measure the hit's shape** — for a write watchpoint, what `code[0]`/`code[1]` carry, and whether the reported program counter is the instruction that wrote or the one after it. **Both go in the pull request**; the second decides whether the engine reports the faulting instruction's address to the caller as-is.

- [ ] **Step 4 to Step 9** — the failing tests (a write watchpoint fires on the global and reports the global's address as the data address; a read watchpoint fires on a read; a 3-byte watchpoint is refused with a diagnostic naming the sizes the architecture can encode; a watchpoint does not modify the target's memory), the encoding per architecture, and the wiring into the same dispatch and propagation Task 5 built.

Branch `feat/machbug-watchpoints`.

---

### Task 7: The breakpoint half of the vtable

**Files:**
- Modify: `src/cross/MachBug/MachBug/api/machbug_api.cpp`
- Test: `src/cross/MachBug/tests/api_contract.cpp` (extend)

**Interfaces:**
- Produces: working `SetBreakpoint`, `DeleteBreakpoint`, `IsBreakpointEffective`, `GetHwBreakpointSlots`, and `onBreakpoint`/`onThreadCreate`/`onThreadExit` forwarded to the C callbacks.

- [ ] **Step 1: Open the issue** — noting that these four are the last `DbgStatus_NotSupported` stubs outside milestone 5's module entries, and that `GetHwBreakpointSlots` must report the sysctl count rather than the array size, because a UI that believes it has 16 slots will ask for the seventh and be refused for no reason it can explain.

- [ ] **Step 2 to Step 6** — the failing contract test (set a breakpoint through the vtable, hit it, receive `onBreakpoint` with the address, delete it, and confirm `IsBreakpointEffective` answers false afterwards; `GetHwBreakpointSlots` reports 6 on this machine and whatever the Intel runner reports there), the implementation with milestone 3's status vocabulary, and the run on both architectures.

Branch `feat/machbug-vtable-breakpoints`.

---

### Task 8: The bench in regview

**Files:**
- Create: `src/cross/views/BreakpointTable.h`, `views/BreakpointTable.cpp`
- Modify: `src/cross/regview/MainWindow.h`, `regview/MainWindow.cpp`
- Modify: `src/cross/views/cmake.toml`, `scripts/verify-vendor.sh`, `docs/COMPILE-macos.md`

- [ ] **Step 1: Open the issue** — a breakpoint list with address, kind and state; add, remove and enable/disable; the four kinds selectable; and the stop reason in the status bar, including a watchpoint's data address. The engine is reached only through the vtable, as `RegisterTable` is.

- [ ] **Step 2 to Step 7** — the widget (a `StdTable` subclass, named for the accessibility tree the way `RegisterTable` is, with the same caveat: issue #88 means a repopulated table currently reads as empty to an accessibility client, so `--selftest` is again what proves it), the wiring, and an extension of `--selftest` that sets a breakpoint on the fixture's published function address, resumes, and fails unless the target stops there with the list showing it armed.

That `--selftest` extension is this task's real deliverable: it is the part CI can run, and milestone 3 established that a screenshot proves the view to a person and nothing to a job.

Branch `feat/regview-breakpoint-bench`.

---

### Task 9: Documentation and the milestone's own record

**Files:**
- Modify: `docs/COMPILE-macos.md`, `docs/specs/2026-09-07-macos-port-design.md`

- [ ] **Step 1: Open the issue** — the spec's milestone 4 row, the sections that describe breakpoints and stepping as future work, and `COMPILE-macos.md`'s account of what `regview` can do.

- [ ] **Step 2 to Step 4** — record what was measured rather than what was planned: the slot counts each architecture reports, the exception encodings Tasks 5 and 6 measured, the program-counter fixup per architecture, and the x86-64 result from the Intel job. Where a measurement contradicted the spec, correct the spec and say it was corrected — milestone 2 set that precedent with `task_for_pid`'s entitlement claim.

Branch `docs/m4-record`.

---

## Milestone 4 exit criteria

On a clean checkout:

```bash
export QT_ROOT_DIR="$(brew --prefix qt)"
./scripts/check-toolchain.sh && ./scripts/verify-vendor.sh && ./scripts/check-credits.sh
cd src/cross
cmake --preset macos-arm64 -DMACHBUG_BUILD_TESTS=ON
cmake --build build/macos-arm64 --target MachBug_tests
./build/macos-arm64/tests/MachBug_tests
cmake --build build/macos-arm64
cd "$(git rev-parse --show-toplevel)" && ./scripts/check-bundles.sh && ./scripts/smoke-launch.sh
QT_QPA_PLATFORM=offscreen \
  ./src/cross/build/macos-arm64/regview.app/Contents/MacOS/regview --selftest \
  ./src/cross/build/macos-arm64/tests/targets/known_function
```

All tests pass, on both architectures in CI, and specifically:

- A software breakpoint stops the target at the address it was set on, and the program counter reported to the caller is that address on both architectures.
- Continuing past a software breakpoint executes the replaced instruction and keeps the breakpoint armed: a target that calls the patched function in a loop hits it more than once.
- A disabled breakpoint stops nothing, and removing one restores the target's original bytes exactly.
- An unaligned arm64 software breakpoint and an unencodable watchpoint size are refused with diagnostics that name what was wrong.
- A hardware execution breakpoint stops the target **without modifying its bytes**, and fires on a thread created after it was armed.
- A write watchpoint fires on a write to the address it watches and reports that data address.
- Asking for one more hardware breakpoint than the architecture has slots fails with a diagnostic naming both numbers, and installs nothing.
- `GetHwBreakpointSlots()` reports the count the platform reports, not the size of the debug-state array.
- `regview --selftest` sets a breakpoint, stops on it, and shows it in the list.

What milestone 4 deliberately does not deliver: conditional breakpoints, breakpoints that survive a target restart (the database is milestone 9), breakpoints on addresses that are not yet mapped (module load is milestone 5), stepping over a call as one operation (step-over needs disassembly, milestone 6), and vector or floating-point registers.
