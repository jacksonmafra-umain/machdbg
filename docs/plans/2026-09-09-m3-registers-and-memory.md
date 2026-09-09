# Milestone 3 — Registers and Memory Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Read and write a stopped target's general-purpose registers and its memory, on arm64 and x86-64, and show that state on screen from a real process.

**Architecture:** The engine gains two leaves and no new layers. `arch/Arm64.cpp` and `arch/X86_64.cpp` each own one thread-state flavor (`thread_get_state`/`thread_set_state`) and one `DbgRegisterDesc` table; `memory/Memory.cpp` owns the `mach_vm_*` calls. `Debugger` keeps the thread port that `handleException()` already receives, which is what makes `threadId == 0` mean "the thread stopped at the current exception" without any thread enumeration. Above the C contract, nothing learns an architecture: the register view is a `StdTable` driven by the descriptor table, so `x0`–`x30` and `rax`–`r15` are rows of data rather than two rendering paths.

**Tech Stack:** C++17, `thread_get_state`/`thread_set_state` with `ARM_THREAD_STATE64` and `x86_THREAD_STATE64`, `mach_vm_read_overwrite`, `mach_vm_write`, `mach_vm_protect`, `mach_vm_region`, `sysctl(KERN_PROC_PID)` for Rosetta detection, Qt 6 with the vendored `StdTable`/`HexDump` widgets, Catch2 v3.14.0, cmkr, `xa11y` for the accessibility check.

**Spec:** `docs/specs/2026-09-07-macos-port-design.md` — section 5 (the contract: architectures, registers, errors and memory), section 6 (MachBug internals: pointer authentication, errors) and section 12 (milestone 3's proof).

**Predecessors:** milestones 0, 1 and 2, complete. `machbug_api.h`'s vtable exists and its lifecycle entries work; `GetRegisters`, `SetRegister`, `GetRegisterDescs` and all six memory entries are stubs returning `DbgStatus_NotSupported` (`src/cross/MachBug/MachBug/api/machbug_api.cpp:257-306`).

## Decisions taken before this plan was written

Settled with the project owner on 2026-09-09. Do not reopen them mid-task.

1. **Proof is the engine *and* the screen.** Milestone 3 delivers the engine plus the path to a populated register view, not engine-only with a headless dump.
2. **The view is new and descriptor-driven.** The vendored `RegistersView` (`src/gui/Src/Gui/RegistersView.h`, a hardcoded x86 `REGISTER_NAME` enum over 3547 lines of `.cpp`) is not extended and not fed arm64 values in x86 slots. A `StdTable` subclass iterates `GetRegisterDescs()` instead — which is what spec section 5 means by "widgets never read struct fields".
3. **`threadId == 0` means the thread stopped at the current exception**; any other value is a `mach_port_t` thread port. No thread enumeration in this milestone — that is milestone 5's `task_threads` work.
4. **`MemWrite` flips protection transparently.** A write into a page without `VM_PROT_WRITE` performs `mach_vm_protect(VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY)`, writes, and restores the original protection. The API has no force flag and gains none; milestone 4's software breakpoints depend on this working.
5. **General-purpose registers only.** No `v0`–`v31`, no `xmm`, no `x87`. The descriptor table makes adding them later additive for every consumer, so they are their own item in a later milestone.

## What was verified before this plan was written

Measured on this machine (Apple Silicon, macOS 26.x, SDK 26.5) on 2026-09-09 with a throwaway probe. Do not re-derive.

- **`ARM_THREAD_STATE64` is flavor `6`, `ARM_THREAD_STATE64_COUNT` is `68`, and `sizeof(arm_thread_state64_t)` is `272`.** Its `__x` array holds **29** entries (`x0`–`x28`); `x29` and `x30` are the separate `__fp` and `__lr` fields, and `__sp`, `__pc`, `__cpsr` follow. A `DbgRegsArm64.x[31]` therefore maps as `x[0..28] = __x[0..28]`, `x[29] = __fp`, `x[30] = __lr`.
- **`x86_THREAD_STATE64` is flavor `4`** (`$(xcrun --show-sdk-path)/usr/include/mach/i386/thread_status.h:106`).
- **`thread_get_state` on a live thread port returns `KERN_SUCCESS`** and plausible `pc`/`sp` (`0x18d2bfc34` / `0x16dd2d1f0` for the probe's own thread).
- **`arm_thread_state64_get_pc(ts)` authenticates the pointer only when the *reading* binary is built for arm64e** (`mach/arm/_structs.h:206` versus `:388` and `:431`). MachBug is built for plain arm64, so those accessors are plain field reads here and are **not** a PAC strip for an arm64e target. Task 2 measures what an arm64e target's `pc` actually looks like before deciding what to strip.
- **`mach_vm_read_overwrite` and `mach_vm_write` work against another task's addresses with only the task port**, no extra entitlement.
- **A bad address returns `KERN_INVALID_ADDRESS` (1)**, from `mach_vm_read_overwrite(task, 0x10, 4, ...)`.
- **A write into a read-execute page fails with `KERN_INVALID_ADDRESS` (1), not `KERN_PROTECTION_FAILURE`.** After `mach_vm_protect(VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY)` the same write returns `KERN_SUCCESS` and the byte is observably changed; restoring `VM_PROT_READ|VM_PROT_EXECUTE` also succeeds. **The error code is the surprising part** — a status mapping that assumes `KERN_PROTECTION_FAILURE` for this case will mis-report it.
- **`mach_vm_region` with `VM_REGION_BASIC_INFO_64` returns base, size, `protection` and `max_protection`** — `prot 5` (`VM_PROT_READ|VM_PROT_EXECUTE`) for the probe's own code region.
- **Page size is 16384 bytes** on Apple Silicon. A protection flip therefore affects at least 16 KB around the target address; Task 4's write must restore protection for exactly the range it changed.
- **Each architecture's thread-state types exist only when compiling *for* that architecture.** `mach/i386/thread_status.h:70` guards its whole contents behind `#if defined(__i386__) || defined(__x86_64__)`, and `mach/arm/thread_status.h:36` does the same for `__arm__ || __arm64__`. Compiling a reference to `x86_thread_state64_t` on Apple Silicon fails with `use of undeclared identifier`. **A descriptor table must therefore be built from `DbgRegsX86_64`/`DbgRegsArm64` offsets, never from the thread-state struct's** — otherwise `Descriptors(DbgArch_X86_64)` cannot even be compiled on an arm64 host, and a view running there could not learn the x86-64 register file at all.
- **`P_TRANSLATED` is `0x00020000`** (`sys/proc.h:178`), which is how a Rosetta target is told apart from a native one for `GetArch()`.

## Global Constraints

- **The six verification scripts must stay green**: `check-toolchain.sh`, `verify-vendor.sh`, `check-credits.sh`, `check-bundles.sh`, `smoke-launch.sh`, `check-build-warnings.sh`. `check-bundles.sh` and `smoke-launch.sh` need a built tree.
- **Anything authored inside `src/` or `cmake/` must be added to `verify-vendor.sh`'s `required[]` in the same commit**, or a future re-vendor deletes it silently. This milestone authors a lot of new files.
- **CI must stay green.** `.github/workflows/macos.yml` runs on every push; a task that needs a new step adds it in its own pull request.
- **Configure and build from inside `src/cross`.** Presets are `src/cross/CMakePresets.json`; export `QT_ROOT_DIR="$(brew --prefix qt)"`. Build engine tests with `-DMACHBUG_BUILD_TESTS=ON`.
- **`CMAKE_OSX_DEPLOYMENT_TARGET` is `14.0`** and comes from the preset. Never hardcode it in a new script or target; `scripts/check-bundles.sh` fails a binary that disagrees with it.
- **`kern_return_t` never crosses the C API** (spec section 5). One translation point produces `DbgStatus`; detail goes through `DbgLastErrorString()`.
- **No architecture check above the C contract.** If a widget or app needs `if (arch == ...)`, the descriptor table is missing something — fix the table.
- **Mirror `ElfBug` where the shape transfers and say so where it does not.** `src/cross/ElfBug/` is vendored, unbuilt, and present as that reference; `ElfBug/ElfBug/thread/Registers.cpp` is the file to read before Task 2.
- **A clean build takes about six minutes** plus Qt deployment. Use targeted builds (`--target MachBug_tests`) while iterating. Never end a turn waiting on a background build.
- **Commits:** English, microcommits. **No assistant attribution of any kind** in commit messages or pull request bodies.
- **Workflow:** one GitHub issue per task assigned to `jacksonmafra-umain`, labelled, milestone `M3 Registers and memory`; one branch per task; one pull request per task **carrying labels, assignee and milestone**; nothing merged without the owner asking.
- **`rm -rf` is denied in this sandbox.** Use a fresh directory or Python's `shutil.rmtree`.
- **`#<issue>` in a pull request body** is the number `gh issue create` printed at the end of the URL in that same task's first step. Substitute it before running the command.
- **Starting a task:** `git checkout main && git pull --ff-only && git checkout -b <the branch named in that task's pull request step>`.
- **Create the milestone once, before Task 1:** `gh api repos/jacksonmafra-umain/machdbg/milestones -f title="M3 Registers and memory" -f description="Registers and memory read/write, both architectures, with a populated register view"` — it fails harmlessly if it already exists.

---

### Task 1: The thread a stop belongs to

`handleException()` already receives the thread port that raised the exception and throws it away (`Debugger.Loop.cpp`, the `thread` parameter is unnamed in the body). Every later task needs it, because registers are per-thread and this milestone deliberately has no thread enumeration.

**Files:**
- Modify: `src/cross/MachBug/MachBug/core/Debugger.h`
- Modify: `src/cross/MachBug/MachBug/core/Debugger.Loop.cpp`
- Test: `src/cross/MachBug/tests/thread_resolution.cpp` (create)
- Modify: `src/cross/MachBug/tests/cmake.toml`
- Modify: `scripts/verify-vendor.sh`

**Interfaces:**
- Produces: `mach_port_t Debugger::ResolveThread(uint64_t threadId) const` — returns the port `handleException()` last received when `threadId == 0`, the `threadId` cast to `mach_port_t` otherwise, and `MACH_PORT_NULL` when there is no such thread. Tasks 2, 3 and 5 call it.
- Produces: `mach_port_t Debugger::StoppedThread() const` — the raw recorded port, `MACH_PORT_NULL` when the target is not stopped at an exception.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Remember which thread a stop belongs to" \
  --assignee jacksonmafra-umain --milestone "M3 Registers and memory" \
  --label "type:feature,area:machbug,prio:high" \
  --body "handleException() is handed the thread port that raised the exception and ignores it. Registers are per-thread, so every register entry point in milestone 3 needs that port, and this milestone has no thread enumeration to look it up with (task_threads is milestone 5).

Record the port for the duration of the stop and expose ResolveThread(threadId): threadId 0 means the thread stopped at the current exception, any other value is a mach_port_t, and an unknown thread resolves to MACH_PORT_NULL so the C API can report a named status instead of calling thread_get_state on a garbage port.

Clear it when the stop ends, so a caller cannot read registers from a thread that is running again and get a torn answer."
```

- [ ] **Step 2: Write the failing test**

Create `src/cross/MachBug/tests/thread_resolution.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <mach/mach.h>
#include <mach/thread_status.h>

#include <MachBug/core/Debugger.h>

#include "TestHarness.h"

#if defined(__arm64__) || defined(__aarch64__)
#include <mach/arm/thread_status.h>
#else
#include <mach/i386/thread_status.h>
#endif

using MachBug::test::EventType;
using MachBug::test::RecordingDebugger;

TEST_CASE("a stop names the thread that raised it, and threadId 0 resolves to it")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    const mach_port_t stopped = debugger.StoppedThread();
    REQUIRE(stopped != MACH_PORT_NULL);
    REQUIRE(debugger.ResolveThread(0) == stopped);
    REQUIRE(debugger.ResolveThread(stopped) == stopped);

    // The port is a real thread of the target, not a number that merely survived being stored.
#if defined(__arm64__) || defined(__aarch64__)
    arm_thread_state64_t state{};
    mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
    const kern_return_t kr = thread_get_state(stopped, ARM_THREAD_STATE64,
                                              reinterpret_cast<thread_state_t>(&state), &count);
#else
    x86_thread_state64_t state{};
    mach_msg_type_number_t count = x86_THREAD_STATE64_COUNT;
    const kern_return_t kr = thread_get_state(stopped, x86_THREAD_STATE64,
                                              reinterpret_cast<thread_state_t>(&state), &count);
#endif
    INFO("thread_get_state returned " << kr << " (" << mach_error_string(kr) << ")");
    REQUIRE(kr == KERN_SUCCESS);

    // An id that names no thread of this target is not silently accepted.
    REQUIRE(debugger.ResolveThread(0xDEADBEEF) == MACH_PORT_NULL);
}

TEST_CASE("no thread is named while the target is running")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::Resumed));
    REQUIRE(debugger.StoppedThread() == MACH_PORT_NULL);
    REQUIRE(debugger.ResolveThread(0) == MACH_PORT_NULL);
}
```

`EventType::Resumed` and `WaitFor` come from `TestHarness.h`; `FIXTURE` is the existing macro the other test files use.

- [ ] **Step 3: Run it to make sure it fails**

```bash
cd src/cross && cmake --preset macos-arm64 -DMACHBUG_BUILD_TESTS=ON >/dev/null \
  && cmake --build build/macos-arm64 --target MachBug_tests 2>&1 | grep -E "error" | head -5
```

Expected: `no member named 'StoppedThread' in 'MachBug::test::RecordingDebugger'`. Capture the exact message for the pull request.

- [ ] **Step 4: Add the two accessors to `Debugger.h`**

In the public section, after `IsRunning()`:

```cpp
        // The thread port that raised the exception the target is currently parked on, or
        // MACH_PORT_NULL when nothing is parked. Registers are per-thread and this milestone has
        // no thread enumeration (task_threads is milestone 5), so this port -- handed to
        // handleException() by MIG and recorded there -- is the only thread identity the engine
        // has. It is valid for exactly as long as the stop is: cleared when the reply goes out.
        mach_port_t StoppedThread() const;

        // Turns the C API's threadId into a thread port. threadId 0 means "the thread stopped at
        // the current exception" -- the case a register view has, since a caller reading
        // registers is by definition looking at a stop. Any other value is taken as a
        // mach_port_t and checked against the target's threads before being returned, so a stale
        // or invented id resolves to MACH_PORT_NULL rather than reaching thread_get_state.
        mach_port_t ResolveThread(uint64_t threadId) const;
```

In the private section, next to `mStopped`:

```cpp
        // Written by handleException() on the loop thread, read by any thread through
        // StoppedThread()/ResolveThread(). Atomic for the same reason mStopped is: the reader is
        // usually the UI thread and the writer is always the loop thread.
        std::atomic<mach_port_t> mStoppedThread{MACH_PORT_NULL};
```

- [ ] **Step 5: Record and clear the port in `Debugger.Loop.cpp`**

In `handleException()`, immediately before the `mStopped.store(true, ...)` that parks the call:

```cpp
        mStoppedThread.store(thread, std::memory_order_release);
```

and immediately after the `mStopped.store(false, ...)` that releases it:

```cpp
        mStoppedThread.store(MACH_PORT_NULL, std::memory_order_release);
```

Then implement the accessors next to `IsStopped()`:

```cpp
    mach_port_t Debugger::StoppedThread() const
    {
        return mStoppedThread.load(std::memory_order_acquire);
    }

    mach_port_t Debugger::ResolveThread(const uint64_t threadId) const
    {
        if(threadId == 0)
            return StoppedThread();

        const auto candidate = static_cast<mach_port_t>(threadId);
        if(!mProcess || mProcess->task == MACH_PORT_NULL)
            return MACH_PORT_NULL;

        // Checked against the task's actual thread list rather than trusted: a caller handing
        // over a stale port from a previous stop, or an outright invented number, would
        // otherwise reach thread_get_state, which happily reports KERN_INVALID_ARGUMENT for a
        // port that is not a thread and MACH_SEND_INVALID_DEST for one that is not even a port.
        // Neither tells a caller "no such thread", which is what actually happened.
        thread_act_array_t threads = nullptr;
        mach_msg_type_number_t count = 0;
        if(task_threads(mProcess->task, &threads, &count) != KERN_SUCCESS)
            return MACH_PORT_NULL;

        mach_port_t found = MACH_PORT_NULL;
        for(mach_msg_type_number_t i = 0; i < count; ++i)
        {
            if(threads[i] == candidate)
                found = candidate;
            mach_port_deallocate(mach_task_self(), threads[i]);
        }
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads),
                      count * sizeof(thread_act_t));
        return found;
    }
```

`task_threads` here is a validity check on one port, not thread enumeration: nothing in this milestone lists threads for a caller, and the ports it borrows are released in the same loop.

- [ ] **Step 6: Wire the new test file into the build**

In `src/cross/MachBug/tests/cmake.toml`, add `thread_resolution.cpp` to the test target's `sources` list, next to `exception_loop.cpp`.

- [ ] **Step 7: Guard the new files against a re-vendor**

Add `src/cross/MachBug/tests/thread_resolution.cpp` to the `required[]` array in `scripts/verify-vendor.sh`.

- [ ] **Step 8: Run the tests**

```bash
cd src/cross && cmake --build build/macos-arm64 --target MachBug_tests 2>&1 | tail -2 \
  && ./build/macos-arm64/tests/MachBug_tests 2>&1 | tail -3
```

Expected: all tests pass, with two more test cases than before.

- [ ] **Step 9: Run the gates and commit**

```bash
cd "$(git rev-parse --show-toplevel)" && ./scripts/verify-vendor.sh | grep -E "FAIL" ; \
  git add -A && git commit -m "Remember the thread port a stop belongs to"
```

- [ ] **Step 10: Open the pull request**

```bash
git push -u origin feat/machbug-stopped-thread
gh pr create --base main --title "Remember which thread a stop belongs to" \
  --assignee jacksonmafra-umain --milestone "M3 Registers and memory" \
  --label "type:feature,area:machbug,prio:high" \
  --body "Closes #<issue>.

handleException() was handed the thread port that raised the exception and dropped it. Registers are per-thread, so every register entry point needs it, and this milestone has no thread enumeration to look one up with.

ResolveThread(threadId) is the contract the C API will use: 0 means the thread stopped at the current exception, any other value is a mach_port_t validated against the task's thread list, and anything unknown resolves to MACH_PORT_NULL so a bad id becomes a named status instead of a thread_get_state error that says nothing about what went wrong.

Test plan: two cases. One proves the recorded port is a real thread (thread_get_state on it succeeds) and that 0 resolves to it; the other proves nothing is named once the target is running again."
```

---

### Task 2: arm64 general registers

**Files:**
- Create: `src/cross/MachBug/MachBug/arch/Arm64.h`, `src/cross/MachBug/MachBug/arch/Arm64.cpp`
- Test: `src/cross/MachBug/tests/registers_arm64.cpp` (create)
- Modify: `src/cross/MachBug/MachBug/cmake.toml`, `src/cross/MachBug/tests/cmake.toml`
- Modify: `scripts/verify-vendor.sh`

**Interfaces:**
- Consumes: `Debugger::ResolveThread` from Task 1.
- Produces, all in `namespace MachBug::arch::Arm64`:
  - `const DbgRegisterDesc* Descriptors(uint32_t* count);`
  - `bool Read(mach_port_t thread, DbgRegisters* out, std::string* error);`
  - `bool Write(mach_port_t thread, const char* name, uint64_t value, std::string* error);`
  - `uint64_t StripPointerAuth(uint64_t address);`
  Tasks 3 and 5 mirror and call these.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Read and write arm64 general registers" \
  --assignee jacksonmafra-umain --milestone "M3 Registers and memory" \
  --label "type:feature,area:machbug,prio:high" \
  --body "thread_get_state/thread_set_state with ARM_THREAD_STATE64 (flavor 6, count 68), mapped into DbgRegsArm64, plus the DbgRegisterDesc table that lets every consumer treat x0-x30/sp/pc/pstate as data.

Two details that are easy to get wrong and are measured facts: arm_thread_state64_t's __x array holds 29 entries, so x29 and x30 are the separate __fp and __lr fields; and arm_thread_state64_get_pc() only authenticates a pointer when the *reading* binary is built for arm64e, which MachBug is not -- so those accessors are not a PAC strip for an arm64e target.

Spec section 6 requires pointer-authentication stripping to happen once, in this file, not scattered across call sites. Measure what an arm64e target's pc actually looks like through thread_get_state before deciding what StripPointerAuth() removes, and say in the code comment what was measured."
```

- [ ] **Step 2: Read the reference**

Read `src/cross/ElfBug/ElfBug/thread/Registers.cpp`. It is the shape to mirror for the name-to-field mapping; what does not transfer is the state flavor and the fact that `x29`/`x30` are not in the array.

- [ ] **Step 3: Write the failing test**

Create `src/cross/MachBug/tests/registers_arm64.cpp`:

```cpp
#if defined(__arm64__) || defined(__aarch64__)

#include <catch2/catch_test_macros.hpp>

#include <string>

#include <MachBug/arch/Arm64.h>
#include <MachBug/core/Debugger.h>

#include "TestHarness.h"

using MachBug::test::EventType;
using MachBug::test::RecordingDebugger;

TEST_CASE("the arm64 descriptor table describes the whole general register file")
{
    uint32_t count = 0;
    const DbgRegisterDesc* descs = MachBug::arch::Arm64::Descriptors(&count);
    REQUIRE(descs != nullptr);
    // x0-x30, sp, pc, pstate.
    REQUIRE(count == 34);

    bool sawPc = false;
    bool sawSp = false;
    for(uint32_t i = 0; i < count; ++i)
    {
        INFO("descriptor " << i << " (" << descs[i].name << ")");
        REQUIRE(descs[i].name != nullptr);
        REQUIRE(descs[i].bits == 64);
        if(std::string(descs[i].name) == "pc")
        {
            sawPc = true;
            REQUIRE((descs[i].flags & DbgRegisterFlag_ProgramCounter) != 0);
        }
        if(std::string(descs[i].name) == "sp")
        {
            sawSp = true;
            REQUIRE((descs[i].flags & DbgRegisterFlag_StackPointer) != 0);
        }
    }
    REQUIRE(sawPc);
    REQUIRE(sawSp);
}

TEST_CASE("a stopped arm64 target reports a plausible pc and sp")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    DbgRegisters regs{};
    std::string error;
    REQUIRE(MachBug::arch::Arm64::Read(debugger.ResolveThread(0), &regs, &error));
    INFO("diagnostic: " << error);
    REQUIRE(regs.arch == DbgArch_Arm64);

    // Not an equality check against a hardcoded address -- ASLR makes that meaningless. What is
    // assertable is that these are addresses at all: a zeroed struct (the failure mode of a
    // Read() that reports success without filling anything) fails both.
    REQUIRE(regs.arm64.pc > 0x1000);
    REQUIRE(regs.arm64.sp > 0x1000);
    REQUIRE(regs.arm64.pc != regs.arm64.sp);
}

TEST_CASE("writing an arm64 register changes what the next read reports")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    const mach_port_t thread = debugger.ResolveThread(0);
    std::string error;

    // x9 rather than pc or sp: a scratch register nothing in the target's next instruction
    // depends on, so the target survives being resumed at the end of this test.
    REQUIRE(MachBug::arch::Arm64::Write(thread, "x9", 0xFEEDFACEull, &error));
    INFO("diagnostic: " << error);

    DbgRegisters regs{};
    REQUIRE(MachBug::arch::Arm64::Read(thread, &regs, &error));
    REQUIRE(regs.arm64.x[9] == 0xFEEDFACEull);
}

TEST_CASE("an unknown register name is refused with a diagnostic")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    std::string error;
    REQUIRE_FALSE(MachBug::arch::Arm64::Write(debugger.ResolveThread(0), "eax", 1, &error));
    INFO("diagnostic: " << error);
    REQUIRE(error.find("eax") != std::string::npos);
}

#endif
```

- [ ] **Step 4: Run it to make sure it fails**

```bash
cd src/cross && cmake --preset macos-arm64 -DMACHBUG_BUILD_TESTS=ON >/dev/null \
  && cmake --build build/macos-arm64 --target MachBug_tests 2>&1 | grep -E "error" | head -3
```

Expected: `'MachBug/arch/Arm64.h' file not found`.

- [ ] **Step 5: Write the header**

Create `src/cross/MachBug/MachBug/arch/Arm64.h`:

```cpp
#pragma once

#include <mach/mach.h>
#include <cstdint>
#include <string>

#include <MachBug/api/machbug_api.h>

// The arm64 half of the register contract: one thread-state flavor (ARM_THREAD_STATE64, flavor
// 6, count 68), one mapping into DbgRegsArm64, one descriptor table. Nothing above the C API
// learns any of this -- that is what the table is for (spec section 5).
//
// This file is also the single place pointer authentication is dealt with (spec section 6): a
// stripped pc must be stripped here, before disassembly, symbolisation or stack walking ever
// sees it, rather than at each call site.
namespace MachBug::arch::Arm64
{
    // The register file as data: x0-x30, sp, pc, pstate. `count` receives the number of entries;
    // the returned pointer is to static storage and outlives every caller.
    const DbgRegisterDesc* Descriptors(uint32_t* count);

    // Fills `out` from `thread`, which must be a thread of a target that is stopped. Returns
    // false and sets `error` (when non-null) to a named diagnostic on failure -- an invalid
    // thread port and a thread that has gone away are both ordinary outcomes here, not
    // programming errors.
    bool Read(mach_port_t thread, DbgRegisters* out, std::string* error);

    // Writes one register by the name the descriptor table publishes. Reads the whole state,
    // replaces one field and writes it back, because thread_set_state has no narrower unit.
    bool Write(mach_port_t thread, const char* name, uint64_t value, std::string* error);

    // Removes an arm64e pointer-authentication signature from a code address. See the comment on
    // the implementation for what was measured about when a signature is actually present.
    uint64_t StripPointerAuth(uint64_t address);
}
```

- [ ] **Step 6: Write the implementation**

Create `src/cross/MachBug/MachBug/arch/Arm64.cpp`:

```cpp
#include <MachBug/arch/Arm64.h>

#include <mach/mach_error.h>
#include <mach/thread_status.h>

#include <cstring>

// arm_thread_state64_t exists only in a build *for* arm64: mach/arm/thread_status.h:36 guards
// its contents behind __arm__ || __arm64__. The descriptor table below is built from
// DbgRegsArm64's own offsets and stays portable, because Descriptors(DbgArch_Arm64) has to
// answer on the Intel runner too -- only the state access is gated.
#if defined(__arm64__) || defined(__aarch64__)
#include <mach/arm/thread_status.h>
#endif

namespace MachBug::arch::Arm64
{
    namespace
    {
        // Built once, at first use, rather than written out as 34 literal initialisers: the
        // offsets are exactly offsetof() arithmetic and the names are exactly "x0".."x30", so
        // spelling them by hand only creates a place for a typo to hide.
        struct Table
        {
            DbgRegisterDesc entries[34];
            char names[31][4];

            Table()
            {
                uint32_t index = 0;
                for(uint32_t i = 0; i < 31; ++i, ++index)
                {
                    std::snprintf(names[i], sizeof(names[i]), "x%u", i);
                    entries[index] = DbgRegisterDesc{
                        index, names[i], 64,
                        static_cast<uint16_t>(offsetof(DbgRegsArm64, x) + i * sizeof(uint64_t)),
                        DbgRegisterFlag_General};
                }
                entries[index++] = DbgRegisterDesc{index, "sp", 64,
                    static_cast<uint16_t>(offsetof(DbgRegsArm64, sp)),
                    DbgRegisterFlag_General | DbgRegisterFlag_StackPointer};
                entries[index++] = DbgRegisterDesc{index, "pc", 64,
                    static_cast<uint16_t>(offsetof(DbgRegsArm64, pc)),
                    DbgRegisterFlag_General | DbgRegisterFlag_ProgramCounter};
                entries[index++] = DbgRegisterDesc{index, "pstate", 64,
                    static_cast<uint16_t>(offsetof(DbgRegsArm64, pstate)),
                    DbgRegisterFlag_Flags};
            }
        };

        const Table& table()
        {
            static const Table instance;
            return instance;
        }

#if defined(__arm64__) || defined(__aarch64__)
        bool getState(mach_port_t thread, arm_thread_state64_t* state, std::string* error)
        {
            if(thread == MACH_PORT_NULL)
            {
                if(error)
                    *error = "no thread to read registers from: the target is not stopped, or the "
                             "thread id names no thread of it";
                return false;
            }
            mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
            const kern_return_t kr = thread_get_state(thread, ARM_THREAD_STATE64,
                reinterpret_cast<thread_state_t>(state), &count);
            if(kr != KERN_SUCCESS)
            {
                if(error)
                    *error = std::string("thread_get_state(ARM_THREAD_STATE64) failed: ") +
                             mach_error_string(kr);
                return false;
            }
            return true;
        }
#endif // defined(__arm64__) || defined(__aarch64__)
    }

    const DbgRegisterDesc* Descriptors(uint32_t* count)
    {
        if(count)
            *count = 34;
        return table().entries;
    }

    uint64_t StripPointerAuth(const uint64_t address)
    {
        // MEASURE THIS BEFORE TRUSTING IT (Step 8 of this task): the accessor macros in
        // mach/arm/_structs.h authenticate only when the *reading* binary is arm64e, and MachBug
        // is plain arm64, so nothing strips this for us. What is left here is the documented
        // shape of a user-space arm64e code pointer: the signature occupies the high bits above
        // the 47-bit virtual address, so masking them off yields the address. If Step 8 measures
        // an unsigned pc for an arm64e target, say so in this comment and keep the function as
        // the single seam anyway -- scattering the decision later is what spec section 6 forbids.
        return address & 0x0000007FFFFFFFFFull;
    }

#if defined(__arm64__) || defined(__aarch64__)

    bool Read(const mach_port_t thread, DbgRegisters* out, std::string* error)
    {
        if(!out)
        {
            if(error)
                *error = "no DbgRegisters to fill";
            return false;
        }
        arm_thread_state64_t state{};
        if(!getState(thread, &state, error))
            return false;

        *out = DbgRegisters{};
        out->arch = DbgArch_Arm64;
        // __x holds 29 entries -- x0 through x28. x29 and x30 are the separate __fp and __lr
        // fields, which is the one place this mapping is not a memcpy.
        for(int i = 0; i < 29; ++i)
            out->arm64.x[i] = state.__x[i];
        out->arm64.x[29] = state.__fp;
        out->arm64.x[30] = state.__lr;
        out->arm64.sp = state.__sp;
        out->arm64.pc = StripPointerAuth(state.__pc);
        out->arm64.pstate = state.__cpsr;
        return true;
    }

    bool Write(const mach_port_t thread, const char* name, const uint64_t value,
               std::string* error)
    {
        if(!name)
        {
            if(error)
                *error = "no register name given";
            return false;
        }
        arm_thread_state64_t state{};
        if(!getState(thread, &state, error))
            return false;

        const std::string wanted(name);
        bool assigned = false;
        for(uint32_t i = 0; i < 29 && !assigned; ++i)
        {
            if(wanted == table().names[i])
            {
                state.__x[i] = value;
                assigned = true;
            }
        }
        if(!assigned && wanted == "x29") { state.__fp = value; assigned = true; }
        if(!assigned && wanted == "x30") { state.__lr = value; assigned = true; }
        if(!assigned && wanted == "sp") { state.__sp = value; assigned = true; }
        if(!assigned && wanted == "pc") { state.__pc = value; assigned = true; }
        if(!assigned && wanted == "pstate") { state.__cpsr = static_cast<uint32_t>(value); assigned = true; }

        if(!assigned)
        {
            if(error)
                *error = "no arm64 register named '" + wanted + "'";
            return false;
        }

        const kern_return_t kr = thread_set_state(thread, ARM_THREAD_STATE64,
            reinterpret_cast<thread_state_t>(&state), ARM_THREAD_STATE64_COUNT);
        if(kr != KERN_SUCCESS)
        {
            if(error)
                *error = std::string("thread_set_state(ARM_THREAD_STATE64) failed: ") +
                         mach_error_string(kr);
            return false;
        }
        return true;
    }

#else

    // Not "unimplemented": there is no arm64 thread state to read on x86-64 hardware. The
    // descriptor table above still answers, because a table is data.
    bool Read(mach_port_t, DbgRegisters* out, std::string* error)
    {
        (void)out;
        if(error)
            *error = "this build cannot read arm64 thread state: it is not an arm64 build";
        return false;
    }

    bool Write(mach_port_t, const char*, uint64_t, std::string* error)
    {
        if(error)
            *error = "this build cannot write arm64 thread state: it is not an arm64 build";
        return false;
    }

#endif // defined(__arm64__) || defined(__aarch64__)
}
```

- [ ] **Step 7: Wire both files into the build**

Add `arch/Arm64.cpp` and `arch/Arm64.h` to the `MachBug` target's `sources`/`headers` globs in `src/cross/MachBug/MachBug/cmake.toml` if the existing globs do not already cover `arch/**`, add `registers_arm64.cpp` to the test target's sources in `src/cross/MachBug/tests/cmake.toml`, and add all three new paths to `verify-vendor.sh`'s `required[]`.

- [ ] **Step 8: Measure what an arm64e target's pc looks like**

The engine is built for arm64; system libraries are arm64e. Attach to a process whose `pc` is inside an arm64e image and print the raw `state.__pc` alongside `StripPointerAuth(state.__pc)`:

```bash
cd src/cross && ./build/macos-arm64/tests/MachBug_tests "a stopped arm64 target reports*" -s 2>&1 | tail -20
```

Then, for the arm64e case specifically, run the same read against `/usr/bin/true`-style system binaries is **not** possible (they are hardened and refuse `task_for_pid` — measured in milestone 2). Instead stop `run_endlessly` at its first exception, where `pc` is inside `dyld` (arm64e), and print both values. Record the two numbers in the pull request body, then either keep the mask or replace the comment with what you measured. **Do not leave the comment claiming something you did not check.**

- [ ] **Step 9: Run the tests**

```bash
cd src/cross && cmake --build build/macos-arm64 --target MachBug_tests 2>&1 | tail -2 \
  && ./build/macos-arm64/tests/MachBug_tests 2>&1 | tail -3
```

Expected: pass, four test cases more than Task 1 left behind.

- [ ] **Step 10: Commit and open the pull request**

```bash
git add -A && git commit -m "Read and write arm64 general registers"
git push -u origin feat/machbug-arm64-registers
gh pr create --base main --title "Read and write arm64 general registers" \
  --assignee jacksonmafra-umain --milestone "M3 Registers and memory" \
  --label "type:feature,area:machbug,prio:high" \
  --body "Closes #<issue>.

ARM_THREAD_STATE64 mapped into DbgRegsArm64, plus the descriptor table that keeps x0-x30/sp/pc/pstate as data for every consumer above the C API.

Two measured details the mapping turns on: arm_thread_state64_t's __x array holds 29 entries, so x29 and x30 come from the separate __fp and __lr fields; and the SDK's pc accessors authenticate only when the reading binary is arm64e, which MachBug is not, so StripPointerAuth() in this file is the only PAC seam (spec section 6).

What an arm64e target's raw pc measured as: <fill in from Step 8, both numbers>.

Test plan: four cases -- the descriptor table covers the file and flags pc/sp, a stopped target reports pc and sp that are addresses rather than zeroes, a write to x9 is visible to the next read, and an x86 register name is refused with a diagnostic naming it."
```

---

### Task 3: x86-64 general registers

Same shape as Task 2, different flavor and field names. It is a separate task because it is separately reviewable and because it is the half that can only be proven on the Intel runner.

**Files:**
- Create: `src/cross/MachBug/MachBug/arch/X86_64.h`, `src/cross/MachBug/MachBug/arch/X86_64.cpp`
- Create: `src/cross/MachBug/MachBug/arch/Arch.h`
- Test: `src/cross/MachBug/tests/registers_x86_64.cpp` (create)
- Modify: `src/cross/MachBug/MachBug/cmake.toml`, `src/cross/MachBug/tests/cmake.toml`, `scripts/verify-vendor.sh`

**Interfaces:**
- Produces `namespace MachBug::arch::X86_64` with the same four functions Task 2 produced for arm64 (`Descriptors`, `Read`, `Write`, and — for symmetry of call sites — no `StripPointerAuth`, since x86-64 has no pointer signing).
- Produces `namespace MachBug::arch`: `const DbgRegisterDesc* Descriptors(DbgArch arch, uint32_t* count);`, `bool Read(DbgArch arch, mach_port_t thread, DbgRegisters* out, std::string* error);`, `bool Write(DbgArch arch, mach_port_t thread, const char* name, uint64_t value, std::string* error);` — the one dispatch point, so `machbug_api.cpp` (Task 5) contains no architecture branch.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Read and write x86-64 general registers" \
  --assignee jacksonmafra-umain --milestone "M3 Registers and memory" \
  --label "type:feature,area:machbug,arch:x86_64,prio:high" \
  --body "x86_THREAD_STATE64 (flavor 4) mapped into DbgRegsX86_64, its descriptor table, and the arch dispatch that keeps machbug_api.cpp free of architecture branches.

This is the half of the milestone that cannot be run on the owner's hardware. It is proven on the macos-15-intel runner, which milestone 1 confirmed exists and reports x86_64 -- the engine tests currently build and run only in the Apple Silicon job, and task 8 of this milestone adds them to the Intel one. Until that lands, an x86-64 assertion is compiled but unproven; say so in the pull request rather than implying otherwise."
```

- [ ] **Step 2: Write the failing test**

Create `src/cross/MachBug/tests/registers_x86_64.cpp`, mirroring `registers_arm64.cpp` with `#if defined(__x86_64__)`, `MachBug::arch::X86_64`, a descriptor count of `21` (the table in Step 4 publishes `rax rbx rcx rdx rbp rsp rsi rdi r8`–`r15`, `rip`, `rflags`, `cs fs gs`), `regs.x86_64.rip`/`.rsp` for the plausibility case, and `"rcx"` as the scratch register the write case sets to `0xFEEDFACE`. The unknown-name case asks for `"x0"` and expects the diagnostic to name it.

```cpp
#if defined(__x86_64__)

#include <catch2/catch_test_macros.hpp>

#include <string>

#include <MachBug/arch/X86_64.h>
#include <MachBug/core/Debugger.h>

#include "TestHarness.h"

using MachBug::test::EventType;
using MachBug::test::RecordingDebugger;

TEST_CASE("a stopped x86-64 target reports a plausible rip and rsp")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    DbgRegisters regs{};
    std::string error;
    REQUIRE(MachBug::arch::X86_64::Read(debugger.ResolveThread(0), &regs, &error));
    INFO("diagnostic: " << error);
    REQUIRE(regs.arch == DbgArch_X86_64);
    REQUIRE(regs.x86_64.rip > 0x1000);
    REQUIRE(regs.x86_64.rsp > 0x1000);
    REQUIRE(regs.x86_64.rip != regs.x86_64.rsp);
}

TEST_CASE("writing an x86-64 register changes what the next read reports")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    const mach_port_t thread = debugger.ResolveThread(0);
    std::string error;
    REQUIRE(MachBug::arch::X86_64::Write(thread, "rcx", 0xFEEDFACEull, &error));
    INFO("diagnostic: " << error);

    DbgRegisters regs{};
    REQUIRE(MachBug::arch::X86_64::Read(thread, &regs, &error));
    REQUIRE(regs.x86_64.rcx == 0xFEEDFACEull);
}

TEST_CASE("an unknown x86-64 register name is refused with a diagnostic")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    std::string error;
    REQUIRE_FALSE(MachBug::arch::X86_64::Write(debugger.ResolveThread(0), "x0", 1, &error));
    INFO("diagnostic: " << error);
    REQUIRE(error.find("x0") != std::string::npos);
}

#endif
```

And, architecture-independent, in the same file outside the `#if`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <MachBug/arch/Arch.h>

TEST_CASE("the dispatch publishes a descriptor table for both architectures")
{
    uint32_t arm = 0;
    uint32_t intel = 0;
    REQUIRE(MachBug::arch::Descriptors(DbgArch_Arm64, &arm) != nullptr);
    REQUIRE(MachBug::arch::Descriptors(DbgArch_X86_64, &intel) != nullptr);
    REQUIRE(arm > 0);
    REQUIRE(intel > 0);

    // A caller asking for an architecture this engine does not implement gets nothing and a
    // zero count, not the host's table by accident -- which would make a wrong register file
    // look like a working one.
    uint32_t unknown = 7;
    REQUIRE(MachBug::arch::Descriptors(DbgArch_I386, &unknown) == nullptr);
    REQUIRE(unknown == 0);
}
```

The dispatch's tables must be available on both hosts — the descriptor table is data, so `Descriptors()` compiles and answers for both architectures everywhere. Only `Read`/`Write` are host-specific, because `thread_get_state` on a foreign flavor is meaningless.

- [ ] **Step 3: Run it to make sure it fails**

```bash
cd src/cross && cmake --build build/macos-arm64 --target MachBug_tests 2>&1 | grep -E "error" | head -3
```

Expected: `'MachBug/arch/X86_64.h' file not found`.

- [ ] **Step 4: Write `X86_64.h`/`X86_64.cpp`**

`X86_64.h` is `Arm64.h` with the namespace changed and **no** `StripPointerAuth`: x86-64 has no signed pointers, and an identity function would invite call sites to strip on both architectures as if the concept were shared.

The mapping is one table and two loops over it, so a name and a field can never disagree:

```cpp
#include <MachBug/arch/X86_64.h>

#include <mach/mach_error.h>
#include <mach/thread_status.h>
#include <mach/i386/thread_status.h>

#include <cstddef>

namespace MachBug::arch::X86_64
{
    namespace
    {
        // THE TABLE IS PORTABLE, THE STATE MAPPING IS NOT. Everything above the `#if` below is
        // built from DbgRegsX86_64's own offsets and compiles on Apple Silicon too, because
        // Descriptors(DbgArch_X86_64) has to answer there -- a view on an arm64 machine still
        // needs to know the x86-64 register file. Everything inside the `#if` touches
        // x86_thread_state64_t, which the SDK does not define unless the compilation is
        // *for* x86-64 (mach/i386/thread_status.h:70).
        //
        // One row per register the thread state can actually fill: x86_thread_state64_t carries
        // __cs, __fs and __gs and *no* __ds/__es/__ss, while DbgRegsX86_64 declares all six --
        // so ds, es and ss are deliberately absent rather than published as rows that always
        // read zero. A row a view cannot trust is worse than a row it never sees; a later
        // milestone that finds a source for them adds them here.
        struct Row
        {
            const char* name;
            uint16_t offset;                 // into DbgRegsX86_64
            uint16_t bits;
            uint32_t flags;
        };

        #define MACHBUG_ROW(field, bits, flags) \
            Row{#field, static_cast<uint16_t>(offsetof(DbgRegsX86_64, field)), bits, flags}

        const Row kRows[] = {
            MACHBUG_ROW(rax, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(rbx, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(rcx, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(rdx, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(rbp, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(rsp, 64, DbgRegisterFlag_General | DbgRegisterFlag_StackPointer),
            MACHBUG_ROW(rsi, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(rdi, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(r8,  64, DbgRegisterFlag_General),
            MACHBUG_ROW(r9,  64, DbgRegisterFlag_General),
            MACHBUG_ROW(r10, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(r11, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(r12, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(r13, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(r14, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(r15, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(rip, 64, DbgRegisterFlag_General | DbgRegisterFlag_ProgramCounter),
            MACHBUG_ROW(rflags, 64, DbgRegisterFlag_Flags),
            MACHBUG_ROW(cs, 16, DbgRegisterFlag_General),
            MACHBUG_ROW(fs, 16, DbgRegisterFlag_General),
            MACHBUG_ROW(gs, 16, DbgRegisterFlag_General),
        };

        #undef MACHBUG_ROW

        constexpr uint32_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);

        struct Table
        {
            DbgRegisterDesc entries[kRowCount];

            Table()
            {
                for(uint32_t i = 0; i < kRowCount; ++i)
                    entries[i] = DbgRegisterDesc{i, kRows[i].name, kRows[i].bits,
                                                 kRows[i].offset, kRows[i].flags};
            }
        };

        const Table& table()
        {
            static const Table instance;
            return instance;
        }

#if defined(__x86_64__)
        // The state offsets, in the same order as kRows, so one index addresses both. Keeping
        // them in a parallel array rather than a field of Row is what keeps Row compilable on a
        // host whose SDK has no x86_thread_state64_t at all.
        const size_t kStateOffsets[] = {
            offsetof(x86_thread_state64_t, __rax), offsetof(x86_thread_state64_t, __rbx),
            offsetof(x86_thread_state64_t, __rcx), offsetof(x86_thread_state64_t, __rdx),
            offsetof(x86_thread_state64_t, __rbp), offsetof(x86_thread_state64_t, __rsp),
            offsetof(x86_thread_state64_t, __rsi), offsetof(x86_thread_state64_t, __rdi),
            offsetof(x86_thread_state64_t, __r8),  offsetof(x86_thread_state64_t, __r9),
            offsetof(x86_thread_state64_t, __r10), offsetof(x86_thread_state64_t, __r11),
            offsetof(x86_thread_state64_t, __r12), offsetof(x86_thread_state64_t, __r13),
            offsetof(x86_thread_state64_t, __r14), offsetof(x86_thread_state64_t, __r15),
            offsetof(x86_thread_state64_t, __rip), offsetof(x86_thread_state64_t, __rflags),
            offsetof(x86_thread_state64_t, __cs),  offsetof(x86_thread_state64_t, __fs),
            offsetof(x86_thread_state64_t, __gs),
        };

        // The two arrays are addressed by one index, so a row added to one and forgotten in the
        // other is a build failure rather than a register that reads someone else's value.
        static_assert(sizeof(kStateOffsets) / sizeof(kStateOffsets[0]) == kRowCount,
                      "every published x86-64 register needs a thread-state offset");

        uint64_t* fieldOf(x86_thread_state64_t* state, uint32_t index)
        {
            return reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(state) +
                                               kStateOffsets[index]);
        }

        bool getState(mach_port_t thread, x86_thread_state64_t* state, std::string* error)
        {
            if(thread == MACH_PORT_NULL)
            {
                if(error)
                    *error = "no thread to read registers from: the target is not stopped, or the "
                             "thread id names no thread of it";
                return false;
            }
            mach_msg_type_number_t count = x86_THREAD_STATE64_COUNT;
            const kern_return_t kr = thread_get_state(thread, x86_THREAD_STATE64,
                reinterpret_cast<thread_state_t>(state), &count);
            if(kr != KERN_SUCCESS)
            {
                if(error)
                    *error = std::string("thread_get_state(x86_THREAD_STATE64) failed: ") +
                             mach_error_string(kr);
                return false;
            }
            return true;
        }
#endif // defined(__x86_64__)
    }

    const DbgRegisterDesc* Descriptors(uint32_t* count)
    {
        if(count)
            *count = kRowCount;
        return table().entries;
    }

#if defined(__x86_64__)

    bool Read(const mach_port_t thread, DbgRegisters* out, std::string* error)
    {
        if(!out)
        {
            if(error)
                *error = "no DbgRegisters to fill";
            return false;
        }
        x86_thread_state64_t state{};
        if(!getState(thread, &state, error))
            return false;

        *out = DbgRegisters{};
        out->arch = DbgArch_X86_64;
        for(uint32_t i = 0; i < kRowCount; ++i)
        {
            // Every field of x86_thread_state64_t is 64 bits wide even where the register is not
            // (__cs, __fs, __gs), so one read shape covers the whole table; the descriptor's
            // `bits` is what tells a view how much of it to show.
            const uint64_t value = *fieldOf(&state, i);
            std::memcpy(reinterpret_cast<uint8_t*>(&out->x86_64) + kRows[i].offset, &value,
                        sizeof(value));
        }
        return true;
    }

    bool Write(const mach_port_t thread, const char* name, const uint64_t value,
               std::string* error)
    {
        if(!name)
        {
            if(error)
                *error = "no register name given";
            return false;
        }
        x86_thread_state64_t state{};
        if(!getState(thread, &state, error))
            return false;

        const std::string wanted(name);
        uint32_t found = kRowCount;
        for(uint32_t i = 0; i < kRowCount; ++i)
        {
            if(wanted == kRows[i].name)
                found = i;
        }
        if(found == kRowCount)
        {
            if(error)
                *error = "no x86-64 register named '" + wanted + "'";
            return false;
        }

        *fieldOf(&state, found) = value;
        const kern_return_t kr = thread_set_state(thread, x86_THREAD_STATE64,
            reinterpret_cast<thread_state_t>(&state), x86_THREAD_STATE64_COUNT);
        if(kr != KERN_SUCCESS)
        {
            if(error)
                *error = std::string("thread_set_state(x86_THREAD_STATE64) failed: ") +
                         mach_error_string(kr);
            return false;
        }
        return true;
    }

#else

    // Not "unimplemented": there is no such thing as reading an x86-64 thread state on arm64
    // hardware. thread_get_state has no flavor for it, the SDK has no struct for it, and a
    // target running under Rosetta exposes the translator's arm64 state, not the emulated one's.
    // Refusing with that sentence in the diagnostic is the honest answer; the descriptor table
    // above still works, because a table is data.
    bool Read(mach_port_t, DbgRegisters*, std::string* error)
    {
        if(error)
            *error = "this build cannot read x86-64 thread state: it is not an x86-64 build";
        return false;
    }

    bool Write(mach_port_t, const char*, uint64_t, std::string* error)
    {
        if(error)
            *error = "this build cannot write x86-64 thread state: it is not an x86-64 build";
        return false;
    }

#endif // defined(__x86_64__)
}
```

`Arm64.cpp` (Task 2) carries the mirror image of this split already. If you are reading these tasks out of order and Task 2's file does not have it, add it there: the whole engine is compiled on the Intel runner by Task 8, so an ungated `arm_thread_state64_t` breaks that build.

The descriptor count the test in Step 2 asserts is `21` with this table. If you publish a different set, change the assertion to match what you published and say why in the pull request — do not leave the number and the table disagreeing.

- [ ] **Step 5: Write `Arch.h` and its dispatch**

```cpp
#pragma once

#include <mach/mach.h>
#include <cstdint>
#include <string>

#include <MachBug/api/machbug_api.h>

// The one place an architecture is chosen. Above this, nothing branches on DbgArch: the C API
// (machbug_api.cpp) calls these three, and the register view iterates whatever table comes back
// (spec section 5).
namespace MachBug::arch
{
    // Answers for both architectures on both hosts -- a descriptor table is data, not a call
    // into the kernel. Returns nullptr and sets *count to 0 for an architecture this engine does
    // not implement, so a caller cannot be handed the wrong register file.
    const DbgRegisterDesc* Descriptors(DbgArch arch, uint32_t* count);

    // Reads/writes through the flavor `arch` names. Only the host's own architecture can be
    // read: thread_get_state has no way to report an x86-64 thread state on arm64 hardware, and
    // a target running under Rosetta is out of scope for this milestone (its thread state is the
    // translator's, not the emulated one's).
    bool Read(DbgArch arch, mach_port_t thread, DbgRegisters* out, std::string* error);
    bool Write(DbgArch arch, mach_port_t thread, const char* name, uint64_t value,
               std::string* error);
}
```

Implement it in `X86_64.cpp`'s translation unit or a small `Arch.cpp` — either is fine, but keep the `#if` for host-specific `Read`/`Write` inside it and out of every caller.

- [ ] **Step 6: Wire into the build and `verify-vendor.sh`, then run the tests**

```bash
cd src/cross && cmake --preset macos-arm64 -DMACHBUG_BUILD_TESTS=ON >/dev/null \
  && cmake --build build/macos-arm64 --target MachBug_tests 2>&1 | tail -2 \
  && ./build/macos-arm64/tests/MachBug_tests 2>&1 | tail -3
```

Expected: pass. On this host the three x86-64 cases compile out; the dispatch case runs.

- [ ] **Step 7: Commit and open the pull request**

```bash
git add -A && git commit -m "Read and write x86-64 general registers"
git push -u origin feat/machbug-x86-registers
gh pr create --base main --title "Read and write x86-64 general registers" \
  --assignee jacksonmafra-umain --milestone "M3 Registers and memory" \
  --label "type:feature,area:machbug,arch:x86_64,prio:high" \
  --body "Closes #<issue>.

x86_THREAD_STATE64 mapped into DbgRegsX86_64, its descriptor table, and MachBug::arch's dispatch so nothing above the C API branches on DbgArch.

One honest gap recorded in the code: x86_thread_state64_t carries __cs, __fs and __gs and no __ds/__es/__ss, while DbgRegsX86_64 declares all six. The table publishes only the fields the thread state can fill rather than rows that always read zero.

Status of the proof: the three x86-64 test cases compile out on Apple Silicon and are **unproven until task 8** adds the engine tests to the macos-15-intel job. The dispatch case runs on both."
```

---

### Task 4: Memory read, write and regions

**Files:**
- Create: `src/cross/MachBug/MachBug/memory/Memory.h`, `src/cross/MachBug/MachBug/memory/Memory.cpp`
- Create: `src/cross/MachBug/tests/targets/known_globals.cpp`
- Test: `src/cross/MachBug/tests/memory.cpp` (create)
- Modify: `src/cross/MachBug/MachBug/cmake.toml`, `src/cross/MachBug/tests/cmake.toml`, `scripts/verify-vendor.sh`

**Interfaces:**
- Consumes: `MachBug::arch::Read` (Task 3's dispatch) in the code-page test, which needs a program counter to aim at, and `Debugger::ResolveThread` (Task 1) to get the thread it reads that from.
- Produces `namespace MachBug::memory`:
  - `bool Read(mach_port_t task, uint64_t address, void* dest, uint64_t size, std::string* error);`
  - `bool Write(mach_port_t task, uint64_t address, const void* src, uint64_t size, std::string* error);`
  - `struct Region { uint64_t base; uint64_t size; uint32_t protection; uint32_t maxProtection; uint32_t userTag; };`
  - `bool RegionOf(mach_port_t task, uint64_t address, Region* out, std::string* error);`
  - `uint32_t EnumRegions(mach_port_t task, Region* out, uint32_t capacity);`
  Task 5 wires all of these to the vtable.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Read and write a target's memory" \
  --assignee jacksonmafra-umain --milestone "M3 Registers and memory" \
  --label "type:feature,area:machbug,prio:high" \
  --body "mach_vm_read_overwrite, mach_vm_write, mach_vm_region and region enumeration, behind MachBug::memory, so the vtable's six memory entries have something to call.

The decided behaviour for a write into a page without VM_PROT_WRITE is a transparent protection flip: mach_vm_protect with VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY, write, restore the original protection. The API has no force flag and gains none; milestone 4's software breakpoints depend on this path working.

Two measured facts to build on. A write into a read-execute page fails with KERN_INVALID_ADDRESS (1), not KERN_PROTECTION_FAILURE -- so a status mapping that assumes the latter mis-reports it. And the page size is 16384 bytes on Apple Silicon, so a flip touches at least 16 KB around the address: restore exactly the range that was changed.

Add a fixture that publishes the address of a known global, so memory assertions are made against a known cell rather than an address someone guessed."
```

- [ ] **Step 2: Write the fixture**

Create `src/cross/MachBug/tests/targets/known_globals.cpp`:

```cpp
// Publishes the address and value of a global, then parks, so a memory test can read and write a
// cell whose address and expected contents it knows -- rather than an address inferred from a
// register, which proves less and breaks differently.
#include <cstdint>
#include <cstdio>
#include <unistd.h>

volatile uint64_t machbug_known_value = 0x0123456789ABCDEFull;

int main()
{
    std::printf("%p %llx\n", (void*)&machbug_known_value,
                (unsigned long long)machbug_known_value);
    std::fflush(stdout);
    for(;;)
        sleep(1);
    return 0;
}
```

Register it in `src/cross/MachBug/tests/cmake.toml` exactly as the six existing fixtures are registered, so it is ad-hoc signed with `get-task-allow` — without that, `task_for_pid` fails and nothing here can run (measured in milestone 2).

- [ ] **Step 3: Write the failing test**

Create `src/cross/MachBug/tests/memory.cpp`. It launches `known_globals`, reads the address it printed, then asserts through `MachBug::memory`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <mach/mach.h>
#include <mach/mach_vm.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include <MachBug/core/Debugger.h>
#include <MachBug/memory/Memory.h>

#include "TestHarness.h"

using MachBug::test::EventType;
using MachBug::test::RecordingDebugger;

namespace
{
    // The fixture prints "<address> <value>" and then parks. Reading that line is how this test
    // knows a cell's address without guessing: nothing else in the engine can tell it yet
    // (module enumeration is milestone 5).
    struct KnownCell
    {
        uint64_t address = 0;
        uint64_t value = 0;
    };
}

TEST_CASE("memory reads and writes reach a known cell in the target")
{
    char outPathTemplate[] = "/tmp/machbug_known_globals.XXXXXX";
    const int outFd = mkstemp(outPathTemplate);
    REQUIRE(outFd != -1);
    const std::string outPath = outPathTemplate;

    // The fixture inherits fd 1 at spawn time, so this process's stdout is redirected to the
    // capture file just for Init() and restored immediately -- the same technique, and the same
    // ordering discipline, as debugger_launch.cpp: nothing is asserted while stdout is
    // redirected, because Catch2's -s trace would otherwise land in the capture file.
    const int savedStdout = dup(STDOUT_FILENO);
    REQUIRE(savedStdout != -1);

    RecordingDebugger debugger;
    std::fflush(stdout);
    const int redirectRc = dup2(outFd, STDOUT_FILENO);
    const bool launched = redirectRc != -1 && debugger.Init(FIXTURE("known_globals").c_str());
    const int restoreRc = dup2(savedStdout, STDOUT_FILENO);
    close(savedStdout);

    REQUIRE(redirectRc != -1);
    REQUIRE(restoreRc != -1);
    REQUIRE(launched);

    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    // Resumed so the fixture can reach its printf, then paused again: memory is inspected while
    // the target is stopped, which is the only state a debugger reads memory in anyway.
    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::Resumed));

    KnownCell cell{};
    for(int attempt = 0; attempt < 500 && cell.address == 0; ++attempt)
    {
        // Polls the capture file for the line rather than sleeping a fixed margin: the fixture
        // prints within milliseconds of resuming, and a poll fails in five seconds with the file
        // contents in hand instead of guessing how long is long enough.
        if(FILE* captured = std::fopen(outPath.c_str(), "r"))
        {
            unsigned long long address = 0;
            unsigned long long value = 0;
            if(std::fscanf(captured, "%llx %llx", &address, &value) == 2)
            {
                cell.address = address;
                cell.value = value;
            }
            std::fclose(captured);
        }
        if(cell.address == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    close(outFd);
    unlink(outPath.c_str());

    debugger.Pause();
    REQUIRE(debugger.IsStopped());

    INFO("the fixture printed address " << std::hex << cell.address << ", value " << cell.value);
    REQUIRE(cell.address != 0);
    REQUIRE(cell.value == 0x0123456789ABCDEFull);
    const mach_port_t task = debugger.GetTaskPort();
    std::string error;

    uint64_t observed = 0;
    REQUIRE(MachBug::memory::Read(task, cell.address, &observed, sizeof(observed), &error));
    INFO("diagnostic: " << error);
    REQUIRE(observed == cell.value);

    const uint64_t replacement = 0xFEEDFACECAFEBEEFull;
    REQUIRE(MachBug::memory::Write(task, cell.address, &replacement, sizeof(replacement), &error));
    REQUIRE(MachBug::memory::Read(task, cell.address, &observed, sizeof(observed), &error));
    REQUIRE(observed == replacement);
}

TEST_CASE("an unmapped address is refused with a diagnostic, not a crash")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    uint64_t destination = 0;
    std::string error;
    REQUIRE_FALSE(MachBug::memory::Read(debugger.GetTaskPort(), 0x10, sizeof(destination),
                                        &destination, &error));
    INFO("diagnostic: " << error);
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("a write into the target's code succeeds and leaves protection as it found it")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    DbgRegisters regs{};
    std::string error;
    // pc at the first stop is inside executable, non-writable memory -- the case milestone 4's
    // software breakpoints need and the reason Write() flips protection at all.
    REQUIRE(MachBug::arch::Read(DbgArch_Arm64, debugger.ResolveThread(0), &regs, &error));
    const uint64_t code = regs.arm64.pc;

    MachBug::memory::Region before{};
    REQUIRE(MachBug::memory::RegionOf(debugger.GetTaskPort(), code, &before, &error));
    REQUIRE((before.protection & VM_PROT_EXECUTE) != 0);
    REQUIRE((before.protection & VM_PROT_WRITE) == 0);

    uint8_t original = 0;
    REQUIRE(MachBug::memory::Read(debugger.GetTaskPort(), code, &original, 1, &error));

    const uint8_t patch = static_cast<uint8_t>(original ^ 0xFF);
    REQUIRE(MachBug::memory::Write(debugger.GetTaskPort(), code, &patch, 1, &error));
    INFO("diagnostic: " << error);

    uint8_t observed = 0;
    REQUIRE(MachBug::memory::Read(debugger.GetTaskPort(), code, &observed, 1, &error));
    REQUIRE(observed == patch);

    MachBug::memory::Region after{};
    REQUIRE(MachBug::memory::RegionOf(debugger.GetTaskPort(), code, &after, &error));
    INFO("protection was " << before.protection << ", is now " << after.protection);
    REQUIRE(after.protection == before.protection);

    // Put the instruction back, so the target is not left corrupted for whatever runs next.
    REQUIRE(MachBug::memory::Write(debugger.GetTaskPort(), code, &original, 1, &error));
}

TEST_CASE("region enumeration finds the region the program counter is in")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    DbgRegisters regs{};
    std::string error;
    REQUIRE(MachBug::arch::Read(DbgArch_Arm64, debugger.ResolveThread(0), &regs, &error));

    MachBug::memory::Region regions[512]{};
    const uint32_t count = MachBug::memory::EnumRegions(debugger.GetTaskPort(), regions, 512);
    REQUIRE(count > 0);

    bool covered = false;
    for(uint32_t i = 0; i < count; ++i)
    {
        if(regs.arm64.pc >= regions[i].base && regs.arm64.pc < regions[i].base + regions[i].size)
            covered = true;
    }
    REQUIRE(covered);

    // Capacity is honoured rather than overrun: asking for one region returns one.
    REQUIRE(MachBug::memory::EnumRegions(debugger.GetTaskPort(), regions, 1) == 1);
}
```

Guard the two cases that name `regs.arm64` with the same `#if defined(__arm64__)` the register tests use, and write the `__x86_64__` twins with `regs.x86_64.rip`. The remaining cases are architecture-independent.

- [ ] **Step 4: Run it to make sure it fails**

Expected: `'MachBug/memory/Memory.h' file not found`.

- [ ] **Step 5: Write `Memory.h`**

```cpp
#pragma once

#include <mach/mach.h>
#include <cstdint>
#include <string>

// The target's address space. Every mach_vm_* call in the engine lives here; nothing above this
// file names one, which is what keeps kern_return_t out of the C API (spec section 5).
namespace MachBug::memory
{
    struct Region
    {
        uint64_t base = 0;
        uint64_t size = 0;
        uint32_t protection = 0;      // current, VM_PROT_* bits
        uint32_t maxProtection = 0;
        uint32_t userTag = 0;         // VM_MEMORY_*
    };

    bool Read(mach_port_t task, uint64_t address, void* dest, uint64_t size, std::string* error);

    // Writes into the target, flipping protection when the page cannot be written as it stands:
    // mach_vm_protect(VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY), write, restore. VM_PROT_COPY is
    // the part that matters for a code page -- it asks for a private copy rather than write
    // access to the shared, signed mapping, which is the only way a debugger gets to patch code
    // at all. The restore covers exactly the range that was changed.
    bool Write(mach_port_t task, uint64_t address, const void* src, uint64_t size,
               std::string* error);

    bool RegionOf(mach_port_t task, uint64_t address, Region* out, std::string* error);

    // Fills up to `capacity` regions, walking the address space from 0, and returns how many
    // were written. A caller that gets `capacity` back should ask again with more room; this
    // milestone's consumer (the memory panel) uses a fixed, generous capacity.
    uint32_t EnumRegions(mach_port_t task, Region* out, uint32_t capacity);
}
```

- [ ] **Step 6: Write `Memory.cpp`**

```cpp
#include <MachBug/memory/Memory.h>

#include <mach/mach_error.h>
#include <mach/mach_vm.h>

namespace MachBug::memory
{
    namespace
    {
        bool guard(mach_port_t task, uint64_t size, std::string* error)
        {
            if(task == MACH_PORT_NULL)
            {
                if(error)
                    *error = "no task port: the engine is not attached to a target";
                return false;
            }
            if(size == 0)
            {
                if(error)
                    *error = "zero-length memory operation";
                return false;
            }
            return true;
        }

        // Two flavors, because neither one answers everything DbgMemoryRegion declares:
        // VM_REGION_BASIC_INFO_64 carries protection *and* max_protection but no user tag, while
        // VM_REGION_EXTENDED_INFO carries the tag and no max. `wantTag` is what lets a caller
        // pay for the second call only when the tag is actually going to be shown -- the write
        // path and MemFindBaseAddr need protection and nothing else, the memory map needs tags.
        bool regionAt(mach_port_t task, mach_vm_address_t address, Region* out, bool wantTag,
                      std::string* error)
        {
            mach_vm_address_t base = address;
            mach_vm_size_t size = 0;
            vm_region_basic_info_data_64_t basic{};
            mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
            mach_port_t object = MACH_PORT_NULL;
            const kern_return_t kr = mach_vm_region(task, &base, &size, VM_REGION_BASIC_INFO_64,
                reinterpret_cast<vm_region_info_t>(&basic), &count, &object);
            if(kr != KERN_SUCCESS)
            {
                if(error)
                    *error = std::string("mach_vm_region failed: ") + mach_error_string(kr);
                return false;
            }
            out->base = base;
            out->size = size;
            out->protection = basic.protection;
            out->maxProtection = basic.max_protection;
            out->userTag = 0;

            if(wantTag)
            {
                mach_vm_address_t tagBase = base;
                mach_vm_size_t tagSize = 0;
                vm_region_extended_info_data_t extended{};
                mach_msg_type_number_t extendedCount = VM_REGION_EXTENDED_INFO_COUNT;
                mach_port_t tagObject = MACH_PORT_NULL;
                if(mach_vm_region(task, &tagBase, &tagSize, VM_REGION_EXTENDED_INFO,
                                  reinterpret_cast<vm_region_info_t>(&extended), &extendedCount,
                                  &tagObject) == KERN_SUCCESS && tagBase == base)
                {
                    out->userTag = extended.user_tag;
                }
                // A tag that could not be read stays 0 rather than failing the whole query: the
                // tag is descriptive, and a region with base, size and protection is still a
                // usable answer without it.
            }
            return true;
        }
    }

    bool Read(mach_port_t task, uint64_t address, void* dest, uint64_t size, std::string* error)
    {
        if(!guard(task, size, error) || !dest)
            return false;

        mach_vm_size_t read = 0;
        const kern_return_t kr = mach_vm_read_overwrite(task, address, size,
            reinterpret_cast<mach_vm_address_t>(dest), &read);
        if(kr != KERN_SUCCESS)
        {
            if(error)
                *error = "reading " + std::to_string(size) + " byte(s) at " +
                         std::to_string(address) + " failed: " + mach_error_string(kr);
            return false;
        }
        if(read != size)
        {
            if(error)
                *error = "short read: asked for " + std::to_string(size) + " byte(s), got " +
                         std::to_string(read);
            return false;
        }
        return true;
    }

    bool Write(mach_port_t task, uint64_t address, const void* src, uint64_t size,
               std::string* error)
    {
        if(!guard(task, size, error) || !src)
            return false;

        const auto attempt = [&]() {
            return mach_vm_write(task, address, reinterpret_cast<vm_offset_t>(src),
                                 static_cast<mach_msg_type_number_t>(size));
        };

        kern_return_t kr = attempt();
        if(kr == KERN_SUCCESS)
            return true;

        // The measured failure for a read-execute page is KERN_INVALID_ADDRESS, not
        // KERN_PROTECTION_FAILURE -- so the flip is attempted for both rather than gated on the
        // code that "should" mean unwritable. A genuinely unmapped address fails the same way
        // twice and is reported below with the second, more specific diagnostic.
        Region region{};
        if(!regionAt(task, address, &region, false, error))
            return false;

        const kern_return_t flipped = mach_vm_protect(task, region.base, region.size, FALSE,
            VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
        if(flipped != KERN_SUCCESS)
        {
            if(error)
                *error = "cannot make " + std::to_string(size) + " byte(s) at " +
                         std::to_string(address) + " writable: " + mach_error_string(flipped) +
                         " (the page is " + std::to_string(region.protection) + ")";
            return false;
        }

        kr = attempt();

        // Restored whether or not the write succeeded, and for exactly the range that was
        // flipped: leaving a target's code page writable is a change to the target this call was
        // never asked to make.
        const kern_return_t restored = mach_vm_protect(task, region.base, region.size, FALSE,
                                                       region.protection);

        if(kr != KERN_SUCCESS)
        {
            if(error)
                *error = "writing " + std::to_string(size) + " byte(s) at " +
                         std::to_string(address) + " failed even after making the page writable: " +
                         mach_error_string(kr);
            return false;
        }
        if(restored != KERN_SUCCESS && error)
            *error = std::string("the write succeeded but the page's original protection could "
                                 "not be restored: ") + mach_error_string(restored);
        return true;
    }

    bool RegionOf(mach_port_t task, uint64_t address, Region* out, std::string* error)
    {
        if(!out)
            return false;
        if(!guard(task, 1, error))
            return false;
        return regionAt(task, address, out, false, error);
    }

    uint32_t EnumRegions(mach_port_t task, Region* out, uint32_t capacity)
    {
        if(task == MACH_PORT_NULL || !out || capacity == 0)
            return 0;

        uint32_t written = 0;
        mach_vm_address_t address = 0;
        while(written < capacity)
        {
            Region region{};
            std::string ignored;
            // The tag is asked for here and nowhere else: enumeration is what feeds a memory map.
            if(!regionAt(task, address, &region, true, &ignored))
                break;
            out[written++] = region;
            const mach_vm_address_t next = region.base + region.size;
            if(next <= address) // No forward progress: stop rather than spin.
                break;
            address = next;
        }
        return written;
    }
}
```

Why `regionAt` takes `wantTag`: `DbgMemoryRegion` declares `protection`, `maxProtection` and `userTag`, and no single `mach_vm_region` flavor answers all three — `VM_REGION_BASIC_INFO_64` has the two protections and no tag, `VM_REGION_EXTENDED_INFO` has the tag and no maximum. The write path and `MemFindBaseAddr` need protections only, so they pay for one call; enumeration, which is what a memory map reads, pays for two.

- [ ] **Step 7: Wire into the build, run the tests, run the gates, commit, open the pull request**

Branch `feat/machbug-memory`. The pull request body states the `maxProtection` decision, quotes the measured `KERN_INVALID_ADDRESS`-for-a-code-page fact, and shows the protection-restored assertion passing.

---

### Task 5: The vtable, for real

**Files:**
- Modify: `src/cross/MachBug/MachBug/api/machbug_api.cpp:236-306`
- Test: `src/cross/MachBug/tests/api_contract.cpp` (add cases)
- Modify: `docs/specs/2026-09-07-macos-port-design.md` if `GetArch`'s Rosetta behaviour differs from what section 5 implies

**Interfaces:**
- Consumes: `Debugger::ResolveThread` (Task 1), `MachBug::arch::{Descriptors,Read,Write}` (Tasks 2 and 3), `MachBug::memory::*` (Task 4).
- Produces: a vtable whose register and memory entries work, and `DbgLastErrorString()` detail for each failure.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Wire registers and memory into the DbgEngine vtable" \
  --assignee jacksonmafra-umain --milestone "M3 Registers and memory" \
  --label "type:feature,area:machbug,prio:high" \
  --body "Replace the nine DbgStatus_NotSupported stubs (GetRegisters, SetRegister, GetRegisterDescs, MemRead, MemWrite, MemFindBaseAddr, MemIsCodePtr, MemIsValidPtr, MemEnumRegions) with calls into MachBug::arch and MachBug::memory, and give GetArch a real answer.

GetArch currently reports the host's architecture with a comment saying milestone 3 will fix it. The real answer needs no register or module access: sysctl(CTL_KERN, KERN_PROC, KERN_PROC_PID) reports p_flag, and P_TRANSLATED (0x00020000) is set for a process running under Rosetta. Host architecture unless translated, x86_64 when translated.

Every failure must leave a named diagnostic behind DbgLastErrorString(), because a status alone cannot distinguish 'the target is running, so there are no registers to read' from 'that thread id names nothing'."
```

- [ ] **Step 2: Write the failing tests**

Add to `src/cross/MachBug/tests/api_contract.cpp`:

```cpp
// A stop reached through the C API only -- no MachBug::Debugger, no MachBug::arch -- because
// what is under test is the contract a Qt view actually calls. Shaped like the launch harness
// already in this file: the callbacks record, Start() owns its own thread, and the target is
// left parked at its first stop for the caller to inspect.
namespace
{
    struct StopHarness
    {
        std::atomic<bool> stopped{false};
        std::atomic<bool> startReturned{false};
        DbgEngine* engine = nullptr;
    };

    void onStopHarnessSystemBreakpoint(void* userdata)
    {
        static_cast<StopHarness*>(userdata)->stopped.store(true, std::memory_order_release);
    }
}

TEST_CASE("the vtable reports registers and memory for a stopped target")
{
    StopHarness harness;
    DbgEngineCallbacks callbacks{};
    callbacks.onSystemBreakpoint = onStopHarnessSystemBreakpoint;
    callbacks.userdata = &harness;

    DbgEngine* engine = MachBugCreate(&callbacks);
    REQUIRE(engine != nullptr);
    harness.engine = engine;

    const std::string path = FIXTURE("run_endlessly");
    DbgLaunchSpec spec{};
    spec.path = path.c_str();

    // Start() blocks for the life of the target (machbug_api.h), and the callback above does not
    // answer the stop, so the target stays parked while every assertion below runs.
    std::thread startThread([&] {
        engine->Start(engine->impl, &spec);
        harness.startReturned.store(true, std::memory_order_release);
    });

    for(int i = 0; i < 500 && !harness.stopped.load(std::memory_order_acquire) &&
                   !harness.startReturned.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const bool stopped = harness.stopped.load(std::memory_order_acquire);

    uint32_t count = 0;
    const DbgRegisterDesc* descs = engine->GetRegisterDescs(engine->impl,
                                                            engine->GetArch(engine->impl), &count);
    REQUIRE(descs != nullptr);
    REQUIRE(count > 0);

    DbgRegisters regs{};
    REQUIRE(engine->GetRegisters(engine->impl, 0, &regs) == DbgStatus_Ok);
    REQUIRE(regs.arch == engine->GetArch(engine->impl));

    // The descriptor table's offsets address the same bytes GetRegisters filled: reading the
    // program counter through the table must produce what the struct field holds. This is the
    // assertion that makes the table trustworthy for a view that only has the table.
    uint64_t throughTable = 0;
    for(uint32_t i = 0; i < count; ++i)
    {
        if((descs[i].flags & DbgRegisterFlag_ProgramCounter) != 0)
            std::memcpy(&throughTable,
                        reinterpret_cast<const uint8_t*>(&regs) +
                            offsetof(DbgRegisters, arm64) + descs[i].offset,
                        sizeof(throughTable));
    }
    REQUIRE(throughTable != 0);

    uint8_t byte = 0;
    REQUIRE(engine->MemRead(engine->impl, throughTable, &byte, 1) == DbgStatus_Ok);
    REQUIRE(engine->MemIsCodePtr(engine->impl, throughTable));
    REQUIRE(engine->MemIsValidPtr(engine->impl, throughTable));
    REQUIRE_FALSE(engine->MemIsValidPtr(engine->impl, 0x10));

    uint64_t base = 0;
    uint64_t size = 0;
    REQUIRE(engine->MemFindBaseAddr(engine->impl, throughTable, &base, &size) == DbgStatus_Ok);
    REQUIRE(base <= throughTable);
    REQUIRE(throughTable < base + size);

    // Torn down the way the attach test in this file does it: Stop(), a bounded wait, join if it
    // finished and detach if it did not, so a regression in teardown is a fast failure rather
    // than a hung test binary or a std::terminate from a joinable thread's destructor.
    engine->Stop(engine->impl);
    for(int i = 0; i < 500 && !harness.startReturned.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if(harness.startReturned.load(std::memory_order_acquire))
        startThread.join();
    else
        startThread.detach();
    MachBugDestroy(engine);

    REQUIRE(stopped);
}

TEST_CASE("the vtable refuses a register read with no stop to read from")
{
    DbgEngineCallbacks callbacks{};
    DbgEngine* engine = MachBugCreate(&callbacks);
    REQUIRE(engine != nullptr);

    DbgRegisters regs{};
    // Never started: there is no target, let alone a stopped thread.
    REQUIRE(engine->GetRegisters(engine->impl, 0, &regs) != DbgStatus_Ok);
    REQUIRE(std::string(DbgLastErrorString()).empty() == false);

    MachBugDestroy(engine);
}
```

The `offsetof(DbgRegisters, arm64)` in the first case is architecture-specific; write it with the same `#if` split the register tests use, so the x86-64 half addresses `offsetof(DbgRegisters, x86_64)`. **The descriptor `offset` field is relative to the active arm of the union** — say that in a comment in `machbug_api.h` as part of this task, because it is exactly the kind of ambiguity that produces a view reading the wrong bytes.

- [ ] **Step 3: Run them to make sure they fail**

Expected: `DbgStatus_NotSupported` from `GetRegisters`, and a null table from `GetRegisterDescs`.

- [ ] **Step 4: Implement the nine entries**

Each follows the same shape as the lifecycle entries already in the file: recover the engine from `impl`, call into `MachBug::arch` or `MachBug::memory`, put any diagnostic into `tlsLastError`, and return a `DbgStatus`. Map failures as:

- no engine, or no target: `DbgStatus_NotAttached`
- target running (nothing stopped) or unknown thread id: `DbgStatus_InvalidArgument`
- `mach_vm_*` failure on a valid task: `DbgStatus_Failed`, with the Mach text in the diagnostic
- target already gone: `DbgStatus_TargetExited`, matching what `Continue`/`Stop` already return in that case

Those are the declared values (`machbug_api.h:17-26`: `Ok`, `Failed`, `InvalidArgument`, `NotAttached`, `NotPermitted`, `NotSupported`, `TargetExited`). Do not invent one; if none fits a case, add it to the header in this task and say why in the pull request.

- [ ] **Step 5: Give `GetArch` a real answer**

```cpp
    DbgArch vtGetArch(void* impl)
    {
        // Host architecture unless the target is translated. A Rosetta process reports
        // P_TRANSLATED (0x00020000, sys/proc.h) in its p_flag, which is the only thing that
        // distinguishes it from a native one without reading its Mach-O header -- and reading
        // that needs the main image's base address, which is milestone 5's work.
        auto* engine = static_cast<MachBugEngine*>(impl);
        if(engine && engine->GetPid() > 0)
        {
            struct kinfo_proc info{};
            size_t length = sizeof(info);
            int name[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, engine->GetPid() };
            if(sysctl(name, 4, &info, &length, nullptr, 0) == 0 && length > 0 &&
               (info.kp_proc.p_flag & P_TRANSLATED) != 0)
                return DbgArch_X86_64;
        }
#if defined(__arm64__) || defined(__aarch64__)
        return DbgArch_Arm64;
#elif defined(__x86_64__)
        return DbgArch_X86_64;
#else
        return DbgArch_Unknown;
#endif
    }
```

Note the limit in the comment: a translated target's *thread state* is the translator's, so `GetRegisters` on one is out of scope for this milestone — reporting the architecture correctly is not the same as being able to read it.

- [ ] **Step 6: Run the whole suite, run the gates, commit, open the pull request**

Branch `feat/machbug-vtable-registers-memory`. The pull request body lists the status mapping chosen for each failure class and quotes the descriptor-offset assertion, since that is the one that makes the table usable by a view.

---

### Task 6: A register view that does not know what an architecture is

**Files:**
- Create: `src/cross/views/RegisterTable.h`, `src/cross/views/RegisterTable.cpp`
- Create: `src/cross/views/cmake.toml`
- Modify: `src/cross/cmake.toml` (add the subdir)
- Modify: `scripts/verify-vendor.sh`

**Interfaces:**
- Produces: `class RegisterTable : public StdTable`, with `void setDescriptors(const DbgRegisterDesc* descs, uint32_t count);`, `void setRegisters(const DbgRegisters& regs);`, and a signal `void registerEdited(const QString& name, uint64_t value);`. Task 7's app connects that signal to `SetRegister`.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Add a descriptor-driven register table widget" \
  --assignee jacksonmafra-umain --milestone "M3 Registers and memory" \
  --label "type:feature,area:gui,prio:high" \
  --body "The vendored RegistersView is a hardcoded x86 REGISTER_NAME enum over 3547 lines; it is not extended and not fed arm64 values in x86 slots. This widget iterates GetRegisterDescs() instead -- name, width, value, flags -- so x0-x30 and rax-r15 are rows of data, which is what spec section 5 means by 'widgets never read struct fields'.

Built on the vendored StdTable rather than from scratch: rendering, selection, column sizing and accessibility (AccessibleStdTable, exercised by tests/accessibility) already work there, and a new QWidget would have to reimplement all four.

Highlights registers that changed since the previous stop, which is the one feature of the upstream view worth having on day one -- it is what makes a register view usable while stepping. Editing a cell emits registerEdited(name, value); the widget never calls the engine itself, so it stays testable without a target."
```

- [ ] **Step 2: Read the two references**

`src/gui/Src/BasicView/StdTable.h` for the API to subclass, and `src/cross/remote_table/` for how an existing sample in this repo populates a `StdTable`.

- [ ] **Step 3: Write the failing test**

The repo has no Qt unit-test target; the widget's proof is Task 7's accessibility check plus a screenshot. What *is* unit-testable without Qt event loops is the value formatting and the change detection, so put those in a free function and test it in the engine test binary:

```cpp
// src/cross/views/RegisterFormat.h -- no Qt, so the engine test target can include it.
#pragma once

#include <cstdint>
#include <string>

#include <MachBug/api/machbug_api.h>

namespace machdbg::views
{
    // Hex, zero-padded to the register's own width, lower case, no 0x prefix -- the shape
    // x64dbg's register view uses, and the reason a 32-bit pstate must not be printed as
    // 16 digits: a value that looks wider than the register is a lie about the register.
    std::string FormatValue(uint64_t value, uint16_t bits);

    // Reads one register out of a DbgRegisters through a descriptor, which is the only way a
    // consumer that has just the table can do it. `offset` is relative to the active arm of the
    // union (see machbug_api.h).
    uint64_t ValueThroughDescriptor(const DbgRegisters& regs, const DbgRegisterDesc& desc);
}
```

```cpp
// src/cross/MachBug/tests/register_format.cpp
#include <catch2/catch_test_macros.hpp>

#include <MachBug/arch/Arch.h>
#include <views/RegisterFormat.h>

TEST_CASE("a register's value is printed at its own width")
{
    REQUIRE(machdbg::views::FormatValue(0xFEEDull, 64) == "000000000000feed");
    REQUIRE(machdbg::views::FormatValue(0xFEEDull, 32) == "0000feed");
    REQUIRE(machdbg::views::FormatValue(0x0ull, 64) == "0000000000000000");
}

TEST_CASE("a register is read through its descriptor, not its struct field")
{
    DbgRegisters regs{};
    regs.arch = DbgArch_Arm64;
    regs.arm64.pc = 0x1234'5678'9ABCull;

    uint32_t count = 0;
    const DbgRegisterDesc* descs = MachBug::arch::Descriptors(DbgArch_Arm64, &count);
    REQUIRE(descs != nullptr);

    bool checked = false;
    for(uint32_t i = 0; i < count; ++i)
    {
        if((descs[i].flags & DbgRegisterFlag_ProgramCounter) == 0)
            continue;
        REQUIRE(machdbg::views::ValueThroughDescriptor(regs, descs[i]) == 0x1234'5678'9ABCull);
        checked = true;
    }
    REQUIRE(checked);
}
```

- [ ] **Step 4: Run it to make sure it fails, then implement `RegisterFormat`**

```cpp
// src/cross/views/RegisterFormat.cpp
#include <views/RegisterFormat.h>

#include <cstdio>
#include <cstring>

namespace machdbg::views
{
    std::string FormatValue(const uint64_t value, const uint16_t bits)
    {
        // Digits, not bytes: a 16-bit register is four hex digits, and a 64-bit one is sixteen.
        // A width taken from sizeof(uint64_t) instead would print cs as 000000000000002b and
        // claim a 64-bit register that does not exist.
        const int digits = bits > 0 ? (bits + 3) / 4 : 16;
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%0*llx", digits,
                      static_cast<unsigned long long>(value));
        return buffer;
    }

    uint64_t ValueThroughDescriptor(const DbgRegisters& regs, const DbgRegisterDesc& desc)
    {
        // The descriptor's offset is relative to the active arm of the union, so the base is
        // whichever arm `regs.arch` names -- not `&regs`, which would be off by the tag.
        const uint8_t* base = nullptr;
        switch(regs.arch)
        {
        case DbgArch_Arm64:
            base = reinterpret_cast<const uint8_t*>(&regs.arm64);
            break;
        case DbgArch_X86_64:
            base = reinterpret_cast<const uint8_t*>(&regs.x86_64);
            break;
        default:
            return 0;
        }

        uint64_t value = 0;
        std::memcpy(&value, base + desc.offset, sizeof(value));

        // Masked to the register's own width: the engine stores a 16-bit segment register in a
        // 64-bit field, and the upper bits are padding, not data.
        if(desc.bits > 0 && desc.bits < 64)
            value &= (1ull << desc.bits) - 1;
        return value;
    }
}
```

- [ ] **Step 5: Implement the widget**

```cpp
// src/cross/views/RegisterTable.h
#pragma once

#include <cstdint>
#include <vector>

#include <MachBug/api/machbug_api.h>

#include "BasicView/StdTable.h"

// The register view, as data. It is handed a descriptor table and a DbgRegisters and knows
// nothing else -- no architecture, no struct fields, no engine (spec section 5). Editing a cell
// emits registerEdited; the widget never calls the engine itself, which is what lets it be
// populated in a test, a sample app or the real debugger without changing.
class RegisterTable : public StdTable
{
    Q_OBJECT

public:
    explicit RegisterTable(QWidget* parent = nullptr);

    // Rebuilds the rows. Call it when the target's architecture becomes known -- once per
    // session in this milestone, since a target does not change architecture mid-run.
    void setDescriptors(const DbgRegisterDesc* descs, uint32_t count);

    // Refills the value column and marks the rows whose value differs from the previous call,
    // which is the feature that makes a register view usable while stepping.
    void setRegisters(const DbgRegisters& regs);

signals:
    void registerEdited(const QString& name, uint64_t value);

private slots:
    void onCellDoubleClicked(int row, int column);

private:
    std::vector<DbgRegisterDesc> mDescs;
    std::vector<uint64_t> mPrevious;
    bool mHavePrevious = false;
};
```

```cpp
// src/cross/views/RegisterTable.cpp
#include "RegisterTable.h"
#include "RegisterFormat.h"

#include <QInputDialog>

RegisterTable::RegisterTable(QWidget* parent)
    : StdTable(parent)
{
    // Named, not merely present: Task 7's accessibility check finds this table by name rather
    // than by position, so a layout change does not silently turn that check into a no-op.
    setAccessibleName("Registers");
    setWindowTitle("Registers");
    addColumnAt(120, "Register", true);
    addColumnAt(200, "Value", true);
    connect(this, &StdTable::doubleClickedSignal, this, [this] {
        onCellDoubleClicked(getInitialSelection(), 1);
    });
}

void RegisterTable::setDescriptors(const DbgRegisterDesc* descs, const uint32_t count)
{
    mDescs.assign(descs, descs + count);
    mPrevious.assign(count, 0);
    mHavePrevious = false;

    setRowCount(static_cast<int>(count));
    for(uint32_t i = 0; i < count; ++i)
    {
        setCellContent(static_cast<int>(i), 0, QString::fromUtf8(mDescs[i].name));
        setCellContent(static_cast<int>(i), 1, QString());
    }
    reloadData();
}

void RegisterTable::setRegisters(const DbgRegisters& regs)
{
    for(std::size_t i = 0; i < mDescs.size(); ++i)
    {
        const uint64_t value = machdbg::views::ValueThroughDescriptor(regs, mDescs[i]);
        const QString text = QString::fromStdString(
            machdbg::views::FormatValue(value, mDescs[i].bits));

        // Only after a first snapshot exists: on the very first stop every register would
        // otherwise read as "changed", which says nothing and trains the eye to ignore the
        // highlight.
        const bool changed = mHavePrevious && value != mPrevious[i];
        setCellContent(static_cast<int>(i), 1, text);
        setCellUserdata(static_cast<int>(i), 1, changed ? 1 : 0);
        mPrevious[i] = value;
    }
    mHavePrevious = true;
    reloadData();
}

void RegisterTable::onCellDoubleClicked(const int row, const int column)
{
    if(row < 0 || row >= static_cast<int>(mDescs.size()) || column != 1)
        return;

    bool accepted = false;
    const QString entered = QInputDialog::getText(this, tr("Edit register"),
        QString(tr("New value for %1 (hex)")).arg(QString::fromUtf8(mDescs[row].name)),
        QLineEdit::Normal, getCellContent(row, 1), &accepted);
    if(!accepted)
        return;

    bool parsed = false;
    const qulonglong value = entered.trimmed().toULongLong(&parsed, 16);
    if(!parsed)
    {
        // Refused rather than silently written as zero: a typo in a register editor is a way to
        // corrupt a debugging session, and the target is the one thing a view must not guess at.
        setCellContent(row, 1, getCellContent(row, 1));
        reloadData();
        return;
    }
    emit registerEdited(QString::fromUtf8(mDescs[row].name), value);
}
```

`StdTable`'s exact method names (`addColumnAt`, `setCellContent`, `setCellUserdata`, `reloadData`, `doubleClickedSignal`, `getInitialSelection`) come from `src/gui/Src/BasicView/StdTable.h`. **Read that header before writing this file and use what is actually there** — the sketch above is the shape, not a promise about a vendored API's spelling. Colour the changed rows through whatever per-cell mechanism that header exposes; if it has none, paint in an overridden `paintContent` and say so.

- [ ] **Step 6: Add the target**

`src/cross/views/cmake.toml` declares a static `machdbg_views` target linking `x64dbg::widgets` and the `MachBug` include directory, and `src/cross/cmake.toml` adds the subdir with `condition = "macos"`. `RegisterFormat.cpp` has no Qt dependency, so add it to **both** `machdbg_views` and the `MachBug_tests` target (or to a tiny `machdbg_register_format` static target both link) — the engine test binary must not acquire a Qt dependency to test a formatting function. Give `MachBug_tests` `src/cross` as an include directory so `<views/RegisterFormat.h>` resolves. Add all new paths to `verify-vendor.sh`.

- [ ] **Step 7: Build, run the tests, commit, open the pull request**

Branch `feat/register-table-widget`.

---

### Task 7: The proof on screen

**Files:**
- Create: `src/cross/regview/main.cpp`, `src/cross/regview/MainWindow.h`, `src/cross/regview/MainWindow.cpp`, `src/cross/regview/cmake.toml`
- Create: `src/cross/tests/accessibility/regview_accessibility.py`
- Create: `docs/screenshots/m3-registers.png`
- Modify: `src/cross/cmake.toml`, `scripts/check-bundles.sh` (add the new app to its list), `scripts/smoke-launch.sh`, `docs/COMPILE-macos.md`, `scripts/verify-vendor.sh`

**Interfaces:**
- Consumes: `machdbg_views`' `RegisterTable` (Task 6) and the vtable (Task 5).
- Produces: `regview.app`, a bundle that launches or attaches a target and shows its registers and memory.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Show a real process's registers and memory in a macOS app" \
  --assignee jacksonmafra-umain --milestone "M3 Registers and memory" \
  --label "type:feature,area:gui,area:machbug,prio:high" \
  --body "Milestone 3's proof is a register view populated from a real process. The debugger app is condition = linux-x64 and its other views need milestones 4 to 6, so this is a sample app in the shape of milestone 1's four: RegisterTable plus a HexDump panel over MemRead, driving the engine through the vtable.

Launch or attach, stop at the first exception, show registers and memory, let a register and a memory byte be edited, resume. That is the whole app; it is also the manual test bench milestone 4 will use for breakpoints.

Proof is not only a screenshot: tests/accessibility already drives a real app through AXUIElement and asserts on what the tables contain (minidump_accessibility.py). A sibling script does the same here, so 'the view is populated' is a check rather than an impression."
```

- [ ] **Step 2: Write the app**

`MainWindow` holds a `RegisterTable` and a `HexDump`, a toolbar with Launch/Attach/Continue/Step, and owns the `DbgEngine*`. `Start()` runs on its own thread (the vtable's `Start` blocks — `machbug_api.h`), callbacks marshal to the UI thread with `Qt::QueuedConnection`, exactly as `debugger/gui/MainWindow.cpp` does for ElfBug. On `onSystemBreakpoint`: `GetRegisters(0)`, `GetRegisterDescs(GetArch())`, populate the table, and read `MemRead` around the program counter into the hex panel. `registerEdited` calls `SetRegister` and re-reads.

- [ ] **Step 3: Add the target and the bundle plumbing**

Follow `src/cross/hex_viewer/cmake.toml` exactly: `qt_executable`, `MACOSX_BUNDLE`, `Info.plist`, icon, `macdeployqt`. Add `regview` to `scripts/check-bundles.sh`'s app list and to `scripts/smoke-launch.sh`. `check-bundles.sh` will then also assert the new bundle's deployment target and `LSMinimumSystemVersion` — expect it to pass at `14.0` and fix the target, not the script, if it does not.

- [ ] **Step 4: Write the accessibility check**

Model it on `src/cross/tests/accessibility/minidump_accessibility.py`. It launches `regview.app` with a fixture path, waits for the first stop, then asserts through `xa11y`:

- a table named `Registers` exists and has at least 30 rows on arm64 (34 descriptors) or at least 20 on x86-64;
- the row named `pc` (or `rip`) holds a 16-digit hex value that is not all zeroes;
- the row named `sp` (or `rsp`) likewise, and differs from the program-counter row;
- the hex panel is non-empty.

The row-count and name assertions are what make this a real check: a view that renders 34 empty rows, or 34 rows of `0000000000000000`, fails it.

- [ ] **Step 5: Take the screenshot**

Launch `regview.app` against `run_endlessly`, capture the window with `screencapture -o -l $(...)` or the same method milestone 1 used for `docs/screenshots/`, and save it as `docs/screenshots/m3-registers.png`. Reference it from `docs/COMPILE-macos.md`.

- [ ] **Step 6: Run every gate, commit, open the pull request**

```bash
cd "$(git rev-parse --show-toplevel)" && export QT_ROOT_DIR="$(brew --prefix qt)" \
  && ./scripts/check-toolchain.sh >/dev/null && ./scripts/verify-vendor.sh | grep FAIL ; \
  ./scripts/check-credits.sh >/dev/null; ./scripts/check-bundles.sh | grep -E "FAIL|regview" ; \
  ./scripts/smoke-launch.sh | tail -3
```

Branch `feat/regview-app`. The pull request body carries the screenshot and the accessibility script's output.

---

### Task 8: Prove the x86-64 half

**Files:**
- Modify: `.github/workflows/macos.yml`
- Modify: `docs/specs/2026-09-07-macos-port-design.md` (research question 3's answer, and section 12's row for milestone 3)

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Run the MachBug engine tests on the Intel runner" \
  --assignee jacksonmafra-umain --milestone "M3 Registers and memory" \
  --label "type:chore,area:build,arch:x86_64,prio:high" \
  --body "The engine tests build and run only in the Apple Silicon job. Milestone 3's x86-64 register code is therefore compiled but unproven, and 'both architectures' in the milestone's definition is currently an assertion rather than a result.

The macos-15-intel image exists and reports x86_64 (milestone 1 measured it, answering research question 3). Build and run MachBug_tests there with the same -DMACHBUG_BUILD_TESTS=ON step and the same wall-clock timeout the Apple Silicon job uses, as the ordinary runner user with no sudo -- the fixtures are signed with get-task-allow at build time, which is what task_for_pid needs (milestone 2 task 1).

If exception delivery or task_for_pid behaves differently on that image, report exactly how it fails. Do not weaken, skip or mark tests expected-to-fail to force the job green."
```

- [ ] **Step 2: Add the steps to the Intel job**

Mirror the Apple Silicon job's three steps — configure with `-DMACHBUG_BUILD_TESTS=ON`, build `MachBug_tests`, run it with `timeout-minutes: 3` — and keep the failure-only diagnosis step that dumps fixture entitlements and recent `EXC_`/`ReportCrash` log activity.

- [ ] **Step 3: Push and read the result**

```bash
gh pr create --base main --title "Run the MachBug engine tests on the Intel runner" \
  --assignee jacksonmafra-umain --milestone "M3 Registers and memory" \
  --label "type:chore,area:build,arch:x86_64,prio:high" \
  --body "Closes #<issue>.

<fill in: the Intel job's test summary line, and whether the three x86-64 register cases passed>"
gh run watch "$(gh run list --limit 1 --json databaseId --jq '.[0].databaseId')" --exit-status
```

- [ ] **Step 4: Record the outcome in the spec**

Section 12's milestone 3 row and the definition-of-done bullet "x86-64 behaviour is either proven on an Intel CI runner or labelled `unverified`" both now have a real answer for registers and memory. Write which it is.

---

## Milestone 3 exit criteria

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
```

All tests pass, and specifically:

- A stopped target's general-purpose registers are read on arm64, and on x86-64 on the Intel runner.
- Writing a register changes what the next read reports, on both architectures.
- Memory is read and written against a cell whose address and expected contents the test knows.
- A write into the target's own executable memory succeeds, and the page's protection is what it was before.
- Region enumeration covers the address the program counter is in, and honours its capacity.
- `threadId == 0` resolves to the thread the current stop belongs to; an unknown id is refused with a diagnostic.
- Every failing entry point leaves a named diagnostic behind `DbgLastErrorString()`.
- `regview.app` shows a real process's registers and memory, and the accessibility check asserts the rows are populated rather than merely present.

What milestone 3 deliberately does not deliver: vector, floating-point and x87 registers; breakpoints of any kind; single-step (`StepInto` remains what milestone 2 made it); module enumeration and the memory-map view; register reads from a target running under Rosetta; and thread enumeration — `task_threads` appears in this milestone only as a validity check on one port.
