# Milestone 6 — Disassembly Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a target's real instructions, on arm64 and x86-64, through Capstone behind the interface the disassembly view already consumes — and retire Zydis.

**Architecture:** The seam already exists and is narrow. `BasicView/Disassembly.cpp` consumes `QZydis` (decoding and instruction-boundary walking) and `ZydisTokenizer` (a classified token stream). This milestone writes `QCapstone` and `CapstoneTokenizer` against the same two interfaces, builds the token stream from `cs_detail` rather than by re-lexing printed text, and takes `zydis_wrapper` off the link line. Nothing above the seam learns which engine decoded the bytes.

**Tech Stack:** C++17, Capstone 5 from Homebrew, Qt 6 with the vendored x64dbg widgets, Catch2 v3.14.0, cmkr.

**Spec:** `docs/specs/2026-09-07-macos-port-design.md` — section 7 (disassembly, the token model, the arm64 analysis gap), decision D5 (Zydis retirement), and section 12's milestone 6 row.

**Predecessors:** milestones 0 through 5, complete and merged. The engine launches, attaches, stops, reads registers and memory, sets breakpoints and watchpoints, steps, tracks threads and modules, and `regview.app` shows all of it with two self-tests running in CI.

## Decisions taken before this plan was written

Settled with the project owner on 2026-09-11. Do not reopen them mid-task.

1. **Capstone comes from Homebrew**, like Qt, rather than being fetched and pinned by CMake. The cost is that the version varies per machine, which this plan handles explicitly rather than hopes about: a toolchain check, a CI install, and a compile-time shim for the constant that was renamed between Capstone 5 and 6.
2. **Zydis is retired in this milestone**, not kept alongside. `QCapstone` and `CapstoneTokenizer` replace both files, `zydis_wrapper` leaves the link line, and x86-64 changes engine at the same time as arm64. Two live disassemblers would mean two token paths and two sources of bugs.
3. **Disassembly only. No assembler.** The spec leaves the assembler an open question (§7: does asmtk parse AArch64? LLVM MC? the Clang integrated assembler?), and nothing in this milestone's definition of done needs one. It is answered when someone measures the candidates, not here.
4. **The view is a tab in `regview`**, beside registers, memory, breakpoints, modules, the map and threads. It already has the engine wiring, the window and two self-tests CI runs; a new sample app would duplicate all of it.

## What was verified before this plan was written

Measured on this machine (Apple Silicon, macOS 26.x) on 2026-09-11. Do not re-derive.

- **Capstone 5.0.9 is installed via Homebrew**, reports API version 5.0, and `cs_support()` answers true for both `CS_ARCH_ARM64` and `CS_ARCH_X86`.
- **The architecture constant here is `CS_ARCH_ARM64`.** The spec warns it became `CS_ARCH_AARCH64` in Capstone 6; that rename has NOT happened at this version, so the shim must keep working on both and must not assume either.
- **`pkg-config --cflags capstone` is a trap.** It answers `-I<prefix>/include/capstone`, so `#include <capstone/capstone.h>` — the include every example uses — does not compile. The include root is `<prefix>/include`. A build that follows pkg-config blindly fails at the first header.
- **Capstone decodes what this project already knows the encodings of.** With `CS_OPT_DETAIL` on, `00 00 20 d4` disassembles as `brk #0` (milestone 4's arm64 software trap) and `fd 7b bf a9` as `stp x29, x30, [sp, #-0x10]!` (the arm64 prologue §7 names as a heuristic). Those are the fixtures Task 2 starts from: bytes this repository has already measured elsewhere.
- **The interface to honour**, from `Disassembler/QZydis.h`: `DisassembleBack`, `DisassembleNext`, `DisassembleAt`, `DecodeDataAt`, `setCodeFoldingManager`, `UpdateConfig`, `UpdateArchitecture`, `getEncodeMap`. `Instruction_t` carries `rva`, `branchDestination`, `length`, `branchType` (`None`/`Conditional`/`Unconditional`/`Call`), `instStr`, `dump`, `regsReferenced` and `tokens`.
- **The token model**, from `ZydisTokenizer.h`: `InstructionToken{ std::vector<SingleToken>, int x }`, `SingleToken{ TokenType, QString text, TokenValue{size, value} }`. The type enum classifies mnemonics, registers, immediates, memory operators and prefixes — which is what a highlighter consumes, and what re-lexing printed text would throw away.
- **The files that leave**: `Disassembler/QZydis.{cpp,h}` (479 + 81 lines), `Disassembler/ZydisTokenizer.{cpp,h}` (759 + 218), and `zydis_wrapper` from `widgets/CMakeLists.txt`.

**Left to measure, by the task that needs it:** what `cs_detail` reports for a conditional branch, an unconditional one and a call on each architecture (Task 4 Step 2), and whether `regsReferenced` can be filled from `cs_regs_access` on both (Task 3 Step 5).

## Global Constraints

- **The six verification scripts must stay green**: `check-toolchain.sh`, `verify-vendor.sh`, `check-credits.sh`, `check-bundles.sh`, `smoke-launch.sh`, `check-build-warnings.sh`.
- **Anything authored inside `src/` or `cmake/` must be added to `verify-vendor.sh`'s `required[]` in the same commit**, and anything deleted must be removed from it in the same commit.
- **New CMake source files need `cmkr gen` in their own directory**, and a new test target needs its entry in the workflow's build step as well as in the scripts.
- **CI must stay green on both architectures**, and both jobs now have to install Capstone.
- **One GitHub issue per task, assigned to `jacksonmafra-umain`; every change lands through a pull request.**
- **Credits and licences**: Capstone is BSD-3-Clause. `CREDITS.md` and `docs/licenses.md` gain it in the task that adds the dependency, and lose Zydis in the task that removes it — `check-credits.sh` is the gate.

---

## Task 1: Capstone as a toolchain dependency

**Files:**
- Modify: `src/cross/widgets/cmake.toml`, `src/cross/widgets/CMakeLists.txt`, `scripts/check-toolchain.sh`, `.github/workflows/macos.yml`, `docs/COMPILE-macos.md`, `CREDITS.md`, `docs/licenses.md`
- Create: `src/cross/widgets/CapstoneVersion.h`

**Interfaces:**
- Produces: a `capstone` link target available to `x64dbg_widgets`, and `MACHDBG_CS_ARCH_ARM64` — the constant to use everywhere instead of naming either spelling directly.

- [ ] **Step 1: Open the issue** — stating that Homebrew is the source by decision, that the version therefore varies per machine, and that this task's whole job is to make that variance visible at configure time instead of at someone else's compile.

- [ ] **Step 2: Write the failing check** — add Capstone to `scripts/check-toolchain.sh` in the shape the Qt check already has: report the version found, fail with a named diagnostic and the `brew install capstone` line when it is absent.

- [ ] **Step 3: Wire CMake.** Find Capstone through `pkg-config` **but correct the include path**: the `.pc` answers `<prefix>/include/capstone` and the headers need `<prefix>/include`. Add the corrected directory, not the raw one.

- [ ] **Step 4: Write `CapstoneVersion.h`** — one header, one decision:

```cpp
#pragma once

#include <capstone/capstone.h>

// Capstone 6 renamed CS_ARCH_ARM64 to CS_ARCH_AARCH64 (spec section 7). Homebrew ships 5.0.9
// here and therefore the old name, but the version is not this project's to choose -- see the
// milestone 6 plan's decision 1 -- so neither spelling is written anywhere else in the port.
#if defined(CS_ARCH_AARCH64)
#define MACHDBG_CS_ARCH_ARM64 CS_ARCH_AARCH64
#else
#define MACHDBG_CS_ARCH_ARM64 CS_ARCH_ARM64
#endif
```

- [ ] **Step 5: Prove it links** — a throwaway translation unit that opens both architectures and closes them, built and run by CI's existing test step, then deleted in the same commit once Task 2's tests cover it properly. (If it cannot be deleted cleanly, keep it as the first test of Task 2 rather than leaving a stub behind.)

- [ ] **Step 6: Install Capstone in both CI jobs**, next to Qt.

- [ ] **Step 7: Credits and licences** — Capstone, BSD-3-Clause, in `CREDITS.md` and `docs/licenses.md`; `./scripts/check-credits.sh` green.

- [ ] **Step 8: Commit.**

---

## Task 2: Decoding

**Files:**
- Create: `src/gui/Src/Disassembler/QCapstone.h`, `QCapstone.cpp`
- Create: `src/cross/tests/disasm/` (a Catch2 target linking Qt Core and the widgets library), `src/cross/tests/disasm/cmake.toml`
- Modify: `src/cross/widgets/cmake.toml`, `scripts/verify-vendor.sh`, `.github/workflows/macos.yml`

**Interfaces:**
- Produces: `QCapstone` with the constructor and methods `QZydis` publishes, filling `Instruction_t`'s `rva`, `length`, `instStr` and `dump`. Tokens and branch information are Tasks 3 and 4.

- [ ] **Step 1: Open the issue.**

- [ ] **Step 2: Write the failing tests** — bytes this repository has already measured, so a wrong answer is recognisable rather than merely different:

```cpp
TEST_CASE("arm64 instructions decode to the text the assembler produced")
{
    // 00 00 20 d4 is BRK #0 -- milestone 4's software trap, verified with `as -arch arm64`.
    // fd 7b bf a9 is STP x29, x30, [sp, #-0x10]! -- the prologue spec section 7 names.
    const uint8_t code[] = {0x00, 0x00, 0x20, 0xd4, 0xfd, 0x7b, 0xbf, 0xa9};
    ...
    REQUIRE(first.length == 4);
    REQUIRE(first.instStr.startsWith("brk"));
    REQUIRE(second.instStr.startsWith("stp"));
}

TEST_CASE("x86-64 instructions decode to the text the assembler produced")
{
    // cc is INT3 -- milestone 4's x86-64 software trap.
    const uint8_t code[] = {0xcc, 0x55, 0x48, 0x89, 0xe5};
    ...
    REQUIRE(first.length == 1);
    REQUIRE(first.instStr.startsWith("int3"));
}
```

- [ ] **Step 3: Run them and watch them fail to compile** — `QCapstone.h` does not exist.

- [ ] **Step 4: Write `QCapstone`.** One `csh` per architecture, opened once in the constructor with `CS_OPT_DETAIL` on and closed in the destructor. `UpdateArchitecture()` chooses which handle `DisassembleAt` uses.

- [ ] **Step 5: Implement `DisassembleBack`/`DisassembleNext`.** These walk instruction boundaries backwards and forwards, which on x86-64 cannot be done by decrementing: back up by a bounded window and decode forwards until the boundary lands on `ip`, exactly as the Zydis implementation does. **Read `QZydis.cpp`'s versions first and keep their shape** — the view depends on their edge behaviour, not only their results.

- [ ] **Step 6: Run the tests and make them pass. Commit.**

- [ ] **Step 7: A decode that fails must produce a length of at least 1** — a zero-length "instruction" makes the view's walk loop forever on undecodable bytes, which every debugger meets in data. Test it with bytes that decode on neither architecture.

---

## Task 3: The token stream

**Files:**
- Create: `src/gui/Src/Disassembler/CapstoneTokenizer.h`, `CapstoneTokenizer.cpp`
- Modify: `QCapstone.cpp`, the disasm tests

**Interfaces:**
- Produces: `CapstoneTokenizer` filling `InstructionToken` with the same `TokenType` classification `ZydisTokenizer` publishes, from `cs_detail` — never by re-lexing `op_str`.

- [ ] **Step 1: Open the issue** — with the spec's reasoning stated: an immediate re-lexed out of printed text is an anonymous number and a register is a word, so the classification the highlighter needs would be gone.

- [ ] **Step 2: Write the failing tests** — assert on *classification*, not on text:

```cpp
TEST_CASE("an arm64 load classifies its register, its memory operator and its immediate")
{
    // ldr x0, [x1, #8]
    ...
    REQUIRE(typeOf(tokens, "x0") == TokenType::GeneralRegister);
    REQUIRE(hasType(tokens, TokenType::MemoryOperator));
    REQUIRE(valueOf(tokens, TokenType::Value) == 8);
}
```

- [ ] **Step 3 to Step 6** — the shared emitter, then a per-architecture operand walker (`cs_arm64_op` and `cs_x86_op`), then `regsReferenced` from `cs_regs_access` — **measure first whether it answers on both architectures at this Capstone version**, and if it does not, say so in the pull request and fill the field from the operand walk instead.

---

## Task 4: Branch and flow information

**Files:** `QCapstone.cpp`, the disasm tests, `docs/specs/2026-09-07-macos-port-design.md`

**Interfaces:**
- Produces: `Instruction_t::branchType` and `branchDestination` filled for both architectures.

- [ ] **Step 1: Open the issue.**

- [ ] **Step 2: Measure what `cs_detail` says** for a conditional branch, an unconditional branch and a call on each architecture — `groups` (`CS_GRP_JUMP`, `CS_GRP_CALL`, `CS_GRP_BRANCH_RELATIVE`) and the operand that carries the destination. **Record all of it in the pull request.** The mapping is written against what was seen.

- [ ] **Step 3 to Step 6** — the classification, the destination, and tests that a `b.eq` is `Conditional`, a `bl` is `Call`, and their destinations are the addresses the encodings name.

- [ ] **Step 7: Document the gap rather than implying parity.** Indirect `br xN` and `blr xN` are `Unconditional`/`Call` with **no** destination this milestone can resolve, and the spec already says so; the test asserts `branchDestination == 0` for them so that nobody later "fixes" it by inventing a number.

---

## Task 5: Retiring Zydis

**Files:**
- Delete: `src/gui/Src/Disassembler/QZydis.{cpp,h}`, `ZydisTokenizer.{cpp,h}`, `src/zydis_wrapper` (per `verify-vendor.sh`'s inventory)
- Modify: `src/cross/widgets/cmake.toml`, `CMakeLists.txt`, `BasicView/Disassembly.{cpp,h}` and every consumer, `scripts/verify-vendor.sh`, `CREDITS.md`, `docs/licenses.md`

- [ ] **Step 1: Open the issue** — naming D5 and the fact that this is the task that makes the milestone's claim true: the link line, not the file count, is what says Zydis is gone.

- [ ] **Step 2 to Step 6** — rename the type in consumers (`Instruction_t::tokens` changes element type), delete the files, drop `zydis_wrapper` from the link line, update `verify-vendor.sh`'s inventory in the same commit, and confirm with `nm`/`otool` that no Zydis symbol remains in the built binaries. That last check is the one that cannot be argued with.

---

## Task 6: The disassembly view

**Files:**
- Create: `src/cross/views/DisassemblyPane.{h,cpp}` (or reuse `BasicView/Disassembly` directly if it needs no engine-specific adapter — decide after Task 5 and say which in the pull request)
- Modify: `src/cross/regview/MainWindow.{h,cpp}`, `main.cpp`, `views/cmake.toml`, `scripts/verify-vendor.sh`, `docs/COMPILE-macos.md`

- [ ] **Step 1: Open the issue.**

- [ ] **Step 2 to Step 6** — a Disassembly tab fed from the engine's memory at the stopped thread's program counter, and the `--selftest` extension: it must report real instructions at the pc, not an empty pane. Assert on content the way the other five views are asserted on — a pane of blank rows renders exactly like a populated one.

---

## Task 7: Documentation and the milestone's own record

**Files:** `docs/COMPILE-macos.md`, `docs/specs/2026-09-07-macos-port-design.md`

- [ ] **Step 1: Open the issue.**

- [ ] **Step 2 to Step 4** — record what was measured rather than what was planned: the Capstone version and the include-path trap, what `cs_detail` reports for each branch shape, whether `cs_regs_access` answered, and the x86-64 result from the Intel job. Where a measurement contradicts the spec, correct it and say it was corrected.

---

## Milestone 6 exit criteria

On a clean checkout:

```bash
export QT_ROOT_DIR="$(brew --prefix qt)"
./scripts/check-toolchain.sh && ./scripts/verify-vendor.sh && ./scripts/check-credits.sh
cd src/cross
cmake --preset macos-arm64 -DMACHBUG_BUILD_TESTS=ON
cmake --build build/macos-arm64 --target MachBug_tests machdbg_disasm_tests
./build/macos-arm64/tests/MachBug_tests
./build/macos-arm64/tests/disasm/machdbg_disasm_tests
cmake --build build/macos-arm64
cd "$(git rev-parse --show-toplevel)" && ./scripts/check-bundles.sh && ./scripts/smoke-launch.sh
QT_QPA_PLATFORM=offscreen \
  ./src/cross/build/macos-arm64/regview.app/Contents/MacOS/regview --selftest \
  ./src/cross/build/macos-arm64/tests/targets/run_endlessly
```

All tests pass, on both architectures in CI, and specifically:

- An arm64 `brk #0` and an x86-64 `int3` — encodings this repository measured in milestone 4 — decode to those mnemonics with those lengths.
- A load classifies its register, its memory operator and its immediate as *those things*, not as words and numbers.
- A conditional branch, an unconditional branch and a call are classified as such, with the destinations their encodings name, and an indirect branch reports no destination rather than a made-up one.
- Undecodable bytes produce a length of at least one, so the view's walk terminates.
- No Zydis symbol is present in any built binary, and `zydis_wrapper` is absent from the link line.
- `regview`'s disassembly tab shows real instructions at the stopped thread's program counter, and `--selftest` fails if it does not.
