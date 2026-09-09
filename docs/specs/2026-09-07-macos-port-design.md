# machdbg — native macOS port design

Date: 2026-09-07
Status: approved, pre-implementation
Supersedes: nothing

## 1. Purpose

Reproduce x64dbg's user experience and workflow — the same views, the same expression parser,
the same command set, the same idea of a plugin — as a native macOS application debugging
native Mach-O processes on Apple Silicon and Intel.

### Non-goals

Stated so nobody drifts into them:

- Debugging Windows PE binaries on macOS. That is Wine or emulation hosting, not a port.
- Running unmodified Windows plugin binaries. Source-level reuse is the target instead (§10).
- Replacing LLDB. This is a UI-and-workflow project on top of a purpose-built engine.
- Debugging processes translated by Rosetta 2.
- Debugging Apple platform binaries protected by System Integrity Protection.

## 2. Decisions

Each of these was chosen over named alternatives. They are settled; reopening one means
revisiting the sections that depend on it.

| # | Decision | Rationale | Rejected |
|---|---|---|---|
| D1 | Native Mach-O engine (`MachBug`), engine API kept backend-neutral | A remote backend is plausible later; retrofitting the seam costs more than reserving it | Remote-only host UI; both engines in v1 |
| D2 | Independent GPLv3 fork named `machdbg` | Design freedom, no maintainer negotiation, clean trademark story | Contributing into `x64dbg/src/cross`; forking the upstream tree directly |
| D3 | Vendor-and-strip: copy upstream at a pinned revision, delete Windows-only code outright | D2 and D5 both require editing core files freely, which a patch-set overlay forbids | Submodule + patch overlay; selective file-by-file port |
| D4 | Universal arm64 + x86_64 from day one | Matches the definition of done; forces the honest register and breakpoint abstractions early | Apple Silicon first; Intel first |
| D5 | Capstone for both architectures; Zydis retired at milestone 6, not at vendor time | One token model, one adapter, one dependency — but the widget library links `zydis_wrapper` today (§4) | Capstone for arm64 only alongside Zydis; LLVM MC; a bespoke decoder |
| D6 | Qt 6 | Qt 5.15 is end-of-life and unsupported on current macOS; official Qt 6 macOS builds are universal, Homebrew's are single-architecture | Qt 5, which D4 rules out |
| D7 | Plugins: source-level tiers, in-process, `dlopen`-based | Lowest risk, keeps the plugin exception meaningful, gives a concrete acceptance test | Out-of-process IPC protocol; deferring plugins past v1 |
| D8 | Ship unsigned with a signing script the user must run | Chosen by the project owner; no paid Apple Developer Program dependency | Developer ID plus notarization; self-signed with deferred notarization |
| D9 | Scylla replaced by a read-only dyld fixup inspector | Import reconstruction is PE-specific; the read-only half is the part users actually read | Dropping it entirely; a full fixup reconstructor |

D8 has a consequence worth stating plainly: signing is not a distribution nicety on macOS. A
binary without `com.apple.security.cs.debugger` cannot call `task_for_pid` and therefore cannot
debug anything. "Unsigned" means the build ships unsigned and the user must run the supplied
signing script before first launch.

## 3. Architecture

```
┌────────────────────────────────────────────────────┐
│ Qt 6 front-end (.app bundle)                       │
│ widgets today — Disassembly, HexDump, Registers,   │
│ table primitives, MemoryPage, Architecture         │
│ to extract — Stack, MemoryMap, Threads, Symbols,   │
│ Graph, Fixups                                      │
└───────────────────────┬────────────────────────────┘
                        │ DbgAdapter (Qt signals, MemoryProvider)
┌───────────────────────▼────────────────────────────┐
│ Core — expression parser, database (JSON + lz4),   │
│ analysis, script engine, command dispatch,         │
│ plugin loader                                      │
└───────────────────────┬────────────────────────────┘
                        │ DbgEngine vtable (C)
┌───────────────────────▼────────────────────────────┐
│ MachBug — Mach exception ports, task_for_pid,      │
│ mach_vm_*, thread_get_state, Mach-O/dyld, DWARF    │
└────────────────────────────────────────────────────┘
```

The vtable boundary is the reserved seam from D1. Nothing above it names MachBug.

The split inside the front-end box is a measured fact, not an estimate. The curated widget list
at commit `8794998` contains `BasicView/Disassembly`, `BasicView/HexDump`, the abstract table
views, `Gui/RegistersView`, `Memory/MemoryPage`, `Disassembler/Architecture` and the accessible
wrappers around them. It does not contain the memory map, thread, symbol or graph views; those
still live only in the Windows shell. The stack view exists as a Linux-side port in
`cross/debugger/gui/CPUStack.cpp` rather than in the library.

Extracting the missing views into the widget library is therefore part of this project, not a
prerequisite someone else has finished. Milestone 5 owns the memory map and thread views,
milestone 7 the symbol view, milestone 9 the graph.

## 4. Repository layout and build

```
machdbg/
├── src/
│   ├── bridge/            vendored; Windows typedefs moved to a portability header
│   ├── dbg/               vendored core: expression parser, database, analysis,
│   │                      script engine, command dispatch, plugin_loader
│   └── cross/
│       ├── widgets/       vendored widget library
│       ├── debugger/      vendored GUI shell and DbgAdapter
│       ├── ElfBug/        vendored, not built — kept as the API reference
│       ├── MachBug/       new
│       └── hex_viewer/ minidump/ remote_table/   vendored, early .app targets
├── deps/                  capstone, asmjit/asmtk, jansson, lz4, yara
├── docs/
├── packaging/             entitlements.plist, sign.sh, Info.plist, bundle rules
└── LICENSE, README.md, CREDITS.md
```

Deleted at vendor time rather than disabled with `#ifdef`: TitanEngine, GleeBug, Scylla,
XEDParse, DeviceNameResolver, and the PE plugin loader path. Dead Windows scaffolding in a fork
is merge noise with no upside.

Two things stay that an earlier draft of this document proposed deleting. Both were corrected
after reading upstream at commit `8794998`.

**`src/gui/Src` stays.** The widget library is not a self-contained directory. `widgets` is
sixteen shim files of its own plus a curated list of 78 files compiled straight out of
`src/gui/Src`, spanning `Accessible`, `BasicView`, `Disassembler`, `Gui`, `Memory`,
`ThirdPartyLibs` and `Utils`. Deleting the Windows Qt shell destroys `x64dbg::widgets` with it.
The upstream extraction is at an earlier stage than the project brief implies. Windows-only
files *outside* the curated list may be removed, but only once that list has been enumerated and
the build proves what is actually referenced.

**Zydis stays until milestone 6.** `x64dbg_widgets` links `zydis_wrapper` today. The coupling is
narrow and already named — `Disassembler/QZydis.{cpp,h}` and
`Disassembler/ZydisTokenizer.{cpp,h}`, with `BasicView/Disassembly.{cpp,h}` as the consumer — so
it is retired when the Capstone tokenizer arrives to replace it (§7), not at vendor time.

`ElfBug` is a deliberate exception of a different kind. It is vendored and never built, because
the engine API is specified as a mirror of `elfbug_api.h` and a mirror needs its original.

Build system: cmkr (`cmake.toml`) throughout the cross tree, matching upstream, so that
hand-merging a TOML file is the merge cost rather than hand-merging generated CMake. New
conditions `macos-arm64` and `macos-x64` sit beside the existing `linux-x64`. One command to
build:

```
cmake --preset macos-universal && cmake --build --preset macos-universal
```

Toolchain: `cmake`, `ninja`, `cmkr` and `capstone` from Homebrew; Qt 6 from the official online
installer, because Homebrew's Qt is single-architecture and D4 requires universal binaries.

`widgets/Qt.cmake` already resolves Qt 6 ahead of Qt 5, so D6 costs less than assumed. The
macOS gap is in the same file: `qt_executable` passes `WIN32` to `qt_add_executable`, sets no
`MACOSX_BUNDLE`, and has only a Windows `windeployqt` step with `macdeployqt` left as a TODO.
No `.app` is produced until that function grows a macOS branch, which is milestone 1 work.

## 5. Engine contract

§4 of the project brief asks for a literal mirror of `elfbug_api.h`, while D1 reserves a seam
for a second backend. Direct global symbols cannot serve two backends, so the contract is a
vtable and the mirror is preserved in the entry points and semantics rather than in the linkage.

```c
typedef struct DbgEngine {
    void*       impl;
    DbgStatus (*Start)(void*, const DbgLaunchSpec*);   /* blocks; owns the event loop */
    DbgStatus (*Continue)(void*);                      /* thread-safe */
    DbgStatus (*StepInto)(void*);
    DbgStatus (*Pause)(void*);
    DbgStatus (*Stop)(void*);
    /* registers, memory, modules, breakpoints */
} DbgEngine;

DbgEngine* MachBugCreate(const DbgEngineCallbacks*);
void       MachBugDestroy(DbgEngine*);
```

`DbgAdapter` sees only `DbgEngine`. A remote backend later fills the same vtable with no adapter
change. The thread-safety split matches `ElfBug`: `Start` blocks and owns the loop, everything
else is callable from another thread.

### Architectures and registers

`DBG_ARCH_X86_64`, `DBG_ARCH_I386`, `DBG_ARCH_ARM64`, `DBG_ARCH_ARM64E`. Two register structs,
`DbgRegsX86_64` and `DbgRegsArm64`, in a tagged union. A flat x86 layout is not available.

Widgets never read struct fields. Each architecture publishes a descriptor table:

```c
typedef struct DbgRegisterDesc {
    uint32_t id;
    const char* name;
    uint16_t bits;
    uint16_t offset;
    uint32_t flags;   /* general, flags register, vector, program counter, stack pointer */
} DbgRegisterDesc;
```

The register view iterates that table. Without it, every view acquires an `if (arch == ARM64)`
branch and the port acquires a permanent maintenance tax. With it, `x0`–`x30`, `sp`, `pc`,
`pstate`, `v0`–`v31` and the x86-64 set are data.

### Breakpoints

The API does not name DR7 or any other architecture-specific register. A breakpoint is a kind —
`SOFTWARE`, `HW_EXEC`, `HW_READ`, `HW_WRITE` — plus an address and a size. The backend chooses
the encoding. `GetHwBreakpointSlots()` is added to the mirrored surface because the available
slot count differs between architectures and the UI has to know.

### Callbacks

The eight from the brief — `onCreateProcess`, `onExitProcess`, `onSystemBreakpoint`,
`onBreakpoint`, `onStep`, `onPaused`, `onError`, `onDebugString` — plus four that macOS forces:

- `onLoadModule` / `onUnloadModule`, because dyld notifies through its own mechanism rather than
  through a debug event equivalent to `LOAD_DLL_DEBUG_EVENT`.
- `onThreadCreate` / `onThreadExit`, because debug registers are per-thread on macOS (§7) and a
  new thread starts without the process's hardware breakpoints.

`onException` covers Mach exceptions that are not breakpoints: `EXC_BAD_ACCESS`,
`EXC_BAD_INSTRUCTION`, `EXC_ARITHMETIC`.

### Errors and memory

`kern_return_t` does not cross the API; a single translation function maps it to `DbgStatus`,
with `DbgLastErrorString()` for detail. A remote backend has no `kern_return_t` to return.

The five memory entry points from the brief are mirrored, plus `MemEnumRegions`, because the
memory map view needs region enumeration and expressing that through `MemIsValidPtr` would be a
workaround rather than an interface.

## 6. MachBug internals

The structural difference from `ElfBug`: that engine stops because `waitpid` returned, while
MachBug stops because a Mach message arrived and has not yet been replied to. The stopped state
*is* a pending unanswered reply, and `Continue` is the act of replying `KERN_SUCCESS`. The
`waitpid` loop shape does not transfer.

```
MachBug/
├── api/machbug_api.h        the DbgEngine vtable (§5)
├── core/Debugger.Loop*.cpp  loop split by event class, mirroring ElfBug
├── platform/                thin Mach wrappers — the testable seam
├── process/ thread/ types/
└── arch/  IArchOps  ->  Arm64.cpp | X86_64.cpp
```

`IArchOps` is what makes D4 survivable: breakpoint encoding, single-step mechanism, debug state
access and the register descriptor table live behind one interface with two implementations, so
no architecture branch appears outside `arch/`.

### Process control

`ptrace` on macOS is limited — no `PT_GETREGS`, no memory access — so it is used only where it
is unavoidable: `PT_TRACE_ME` when launching, `PT_ATTACHEXC` when attaching, `PT_DETACH` when
finishing. Everything else goes through Mach. Launching uses `posix_spawn` with
`POSIX_SPAWN_START_SUSPENDED`, which gives a clean window to install exception ports before the
target executes an instruction.

### The loop

Two threads. The thread that called `Start` installs the exception ports —
`task_set_exception_ports` with `EXC_MASK_BREAKPOINT | EXC_MASK_BAD_ACCESS |
EXC_MASK_BAD_INSTRUCTION | EXC_MASK_ARITHMETIC | EXC_MASK_SOFTWARE`, behaviour
`EXCEPTION_STATE_IDENTITY | MACH_EXCEPTION_CODES` — and enters a `mach_msg` receive loop served
by the MIG-generated `mach_exc_server`. External commands arrive through a queue guarded by a
mutex and condition variable; that queue is what makes `Continue`, `StepInto`, `Pause` and
`Stop` thread-safe. `Pause` is `task_suspend`, not a signal.

### Stepping and breakpoints

Single-step is the TF bit in `rflags` on x86-64, and the SS bit in `PSTATE` together with
`mdscr_el1` via `ARM_DEBUG_STATE64` on arm64. Two mechanisms, one `IArchOps` entry.

Software breakpoints are `0xCC` on x86-64 and a 4-byte `BRK #0` at a 4-byte-aligned address on
arm64. Writing to a code page on arm64 requires `mach_vm_protect` with `VM_PROT_COPY`, otherwise
the protection change fails on shared pages — the W^X friction the brief warns about. Stepping
over a breakpoint is the restore, single-step, re-arm cycle.

Hardware breakpoints use `x86_DEBUG_STATE64` or `ARM_DEBUG_STATE64` with BVR/BCR and WVR/WCR
pairs, which differ in count, semantics and encoding. The trap: on macOS debug state is
per-thread, not per-process, so every new thread starts without them. `onThreadCreate` is the
hook that propagates the state, which is why it is in the API rather than an optional extra.

### Pointer authentication

On arm64e, `pc` and `lr` arrive signed. Stripping happens once, in `arch/Arm64.cpp`, before
disassembly, symbolisation or stack walking. It must not be scattered across call sites.

### Errors

One function translates `kern_return_t`. A denied `task_for_pid` is the most common failure in
the whole product and must surface as a named diagnostic, not as an unknown error — it is the
entry point to the debuggability report in §9.

## 7. Disassembly, assembly and analysis

Capstone serves both architectures, one handle each, created once, with `CS_OPT_DETAIL`
enabled. The AArch64 architecture constant was renamed between Capstone 5 and 6
(`CS_ARCH_ARM64` became `CS_ARCH_AARCH64`); confirm against the installed header rather than
assuming.

### Token model

The highlighter consumes a classified token stream — mnemonic, register, immediate, memory
bracket, prefix — not printed text. With Zydis gone, that stream is built from `cs_detail`:
a shared token emitter plus a per-architecture operand walker behind `IArchOps`.

Re-lexing Capstone's printed string was considered and rejected. It is cheaper but discards
classification: an immediate becomes an anonymous number, a register becomes a word. Building
from structured operands is the difference between one highlighter and two.

The seam already exists upstream and is narrow: `Disassembler/QZydis.{cpp,h}` and
`Disassembler/ZydisTokenizer.{cpp,h}`, with `BasicView/Disassembly.{cpp,h}` as the consumer and
`Disassembler/Architecture.{cpp,h}` alongside. The work is a `QCapstone` and
`CapstoneTokenizer` pair honouring the same interface, after which `zydis_wrapper` leaves the
link line. That retirement is milestone 6, per D5.

### Assembler

Open question, deliberately not answered here (§13). asmjit has an AArch64 backend and a clean
zlib licence, but the *parser* is the problem: asmtk has historically assembled x86 only, and
its AArch64 parsing support must be verified. Keystone is excluded on licence grounds — it is
GPLv2-only, which is incompatible with a GPLv3 project. The remaining candidates are asmtk if
it parses AArch64, LLVM MC used purely as an assembler, or invoking the Clang integrated
assembler.

### Analysis on arm64

New prologue and epilogue heuristics (`stp x29, x30, [sp, #-N]!`, `pacibsp`, `retab`), no SEH,
and PAC awareness throughout. An honest limitation: control-flow recovery is weaker than on
x86. Indirect `br xN` and `blr xN` are common in Apple code — jump tables, stubs, Objective-C
dispatch — so the graph view will have gaps its Windows counterpart does not. `LC_FUNCTION_STARTS`
(§8) partially compensates. Document the gap rather than implying parity.

### Strings and modules

Mach-O aware: `__TEXT,__cstring`, `__TEXT,__objc_methname`, and CFString data in
`__DATA,__cfstring`. C++ demangling already exists in the vendored core; Objective-C selectors
come along with the section parsing. Swift demangling is out of scope for v1 — it is a
dependency of its own and blocks nothing in the definition of done.

## 8. Modules, symbols, memory map and fixups

One layering decision carries this section: the Mach-O parser knows nothing about Mach. It takes
a byte-reader interface, and the bytes come either from a file on disk or from
`mach_vm_read_overwrite` against the live process. One parser, two sources, testable without a
running process.

This is a requirement, not a preference. System dylibs live in the dyld shared cache and have no
corresponding file on disk, so for those, process memory is the only available source. Any
design that assumes "open the path and parse it" fails on the first `libSystem`.

### Enumeration

`task_info(TASK_DYLD_INFO)` yields `dyld_all_image_infos`, whose `infoArray` gives
`imageLoadAddress` and `imageFilePath` per image. The ASLR slide is the real load address minus
the `__TEXT` segment's `vmaddr`. Live load and unload events come from the `notification`
function pointer in the same structure: an internal breakpoint there is what fires
`onLoadModule` and `onUnloadModule` from §5.

Load commands that matter: `LC_SEGMENT_64`, `LC_SYMTAB`, `LC_DYSYMTAB`, `LC_UUID`,
`LC_LOAD_DYLIB`, `LC_MAIN`, `LC_DYLD_CHAINED_FIXUPS` (or the legacy `LC_DYLD_INFO_ONLY`), and
`LC_FUNCTION_STARTS`.

`LC_FUNCTION_STARTS` deserves emphasis: it provides function boundaries directly from the
binary, which is exactly the remedy for the control-flow recovery gap admitted in §7.

### Symbols

Two sources. `nlist_64` entries from `LC_SYMTAB` in memory, which partially survive stripping.
And DWARF from a `.dSYM` bundle, located by `LC_UUID` through
`mdfind com_apple_xcode_dsym_uuids`. There is no symbol server to fall back on, so dSYM
management needs its own UI.

The DWARF parser is a research question (§13), not a decision made here. Writing one is out of
the question; LLVM DebugInfo is heavy and contradicts D5; libdwarf is the leading candidate
subject to a licence check.

### Memory map

`mach_vm_region_recurse`, exposing submaps, current and maximum protection, share mode and the
user tag (`VM_MEMORY_MALLOC`, `VM_MEMORY_STACK`, `VM_MEMORY_DYLIB` and others). The taxonomy is
not the Windows one, so tags are mapped to readable names and regions are correlated with
modules.

### Threads

`task_threads` for enumeration, `thread_info(THREAD_BASIC_INFO)` for state,
`THREAD_IDENTIFIER_INFO` for the thread id, and `THREAD_EXTENDED_INFO` for `pth_name` — thread
names cannot be read cross-process through `pthread_getname_np`.

### Fixup inspector

Per D9, read-only. It parses `dyld_chained_fixups_header`, the starts structures and the import
table, and displays stub-to-target resolution beside the module view. Nothing is rewritten, so
code signature invalidation never arises.

## 9. Signing, entitlements, SIP and patching

### Corrected: what `task_for_pid` actually requires (measured, milestone 2 task 1)

This section originally claimed `com.apple.security.cs.debugger` on an Apple-issued identity was
required before `task_for_pid` would work at all. That is wrong for the case this milestone's
tests depend on, and it was measured rather than assumed:

| Caller | Target | Result |
|---|---|---|
| Ad-hoc signed, no debugger entitlement | Ad-hoc signed **with** `get-task-allow` | `task_for_pid` succeeds |
| Ad-hoc signed, no debugger entitlement | Ad-hoc signed **without** `get-task-allow` | `KERN_FAILURE` (5) |
| Ad-hoc signed, no debugger entitlement | System binary, hardened runtime | `KERN_FAILURE` (5) |

The rule this measurement establishes: **`get-task-allow` on the target is what `task_for_pid`
needs from a same-user caller.** A caller with no debugger entitlement at all reaches any target
that carries it. `com.apple.security.cs.debugger` (Apple-issued identity required, see below) is
not the price of entry for our own fixtures — it is what buys a caller access to targets that did
*not* opt in: third-party apps, hardened-runtime binaries, anything the debugger does not build
and sign itself. Milestone 2's engine tests build and sign their own targets with
`get-task-allow` (`src/cross/MachBug/tests/targets.entitlements`), so they need none of the
machinery below to pass; the Apple-issued identity is only load-bearing once machdbg has to
attach to processes it did not launch.

### The debugger's own signature

`com.apple.security.cs.debugger` is honoured only for a binary signed with an Apple-issued
identity; an ad-hoc signature (`codesign -s -`) does not qualify. It is what lets the debugger
attach to targets that never opted in themselves — a third-party app, a hardened-runtime binary,
anything without `get-task-allow`. The user must also be in the `_developer` group, via
`DevToolsSecurity -enable`.

`packaging/sign.sh` therefore does three things: locates an identity with
`security find-identity -v -p codesigning`, fails with a named instruction if there is none, and
signs with the hardened runtime and `packaging/entitlements.plist`. Failing quietly here
produces an application that launches and debugs nothing, which is the worst available failure
mode.

`entitlements.plist` carries `com.apple.security.cs.debugger` and, per §10,
`com.apple.security.cs.disable-library-validation`.

### The target's debuggability

| Target | Result |
|---|---|
| Built by the user, carries `get-task-allow` | Works — from any caller, even one with no debugger entitlement |
| Third-party notarised app with hardened runtime | Not attachable; only a re-signed copy is, or a caller holding `com.apple.security.cs.debugger` on an Apple-issued identity |
| Apple binary protected by SIP | Out of reach while SIP is enabled |
| Any target with SIP disabled | More becomes reachable — the user's choice, never our requirement |

### Debuggability report

A first-class feature, not an error string. Before attaching, machdbg answers: is SIP enabled,
does the target carry `get-task-allow`, does it use the hardened runtime, is it Apple-signed, do
we hold the debugger entitlement, is the user in `_developer`. This is what turns "attach
failed" into "this app is notarised without `get-task-allow`; the only route is re-signing a
copy".

### Target preparation helper

Opt-in, and always operating on a copy: re-sign with `get-task-allow`. The cost is stated up
front — the original signature is destroyed, and with it keychain access, iCloud entitlements
and anything else that depended on it. Never in place.

### Patching

On arm64 a valid code signature is mandatory for execution. A patched binary that is not
re-signed does not run at all; the kernel refuses it. Patching and `codesign -f -s -` are
therefore a single step in the workflow, with a warning that notarisation and entitlements are
lost. Without this, the patching feature is born broken on Apple Silicon.

## 10. Plugins

### Library validation

An application with the hardened runtime enabled has library validation on, and `dlopen` of a
dylib signed by another team is refused. A third-party plugin loader therefore requires
`com.apple.security.cs.disable-library-validation` in the entitlements. Without that line the
loader compiles, runs, and rejects every plugin that is not the user's own.

### Loader

`dlopen` and `dlsym`, with the extension `.dpmac` — distinct from `.dylib` because the ABI is
different and conflating them creates support confusion. `PLUG_EXPORT` becomes
`extern "C" __attribute__((visibility("default")))`. The resolved symbols are unchanged:
`pluginit`, `plugsetup`, `plugstop`, `CBALLEVENTS`, and one export per callback name.

### Windows types in the ABI

| Leak | Decision |
|---|---|
| `HWND hwndDlg` in `PLUG_SETUPSTRUCT` | Opaque `void*` carrying a `QWidget*` |
| `MSG* message` in `PLUG_CB_WINEVENT` | Symbol kept, never fires. Removing it breaks Tier 2 compilation for no gain |
| `DbgGetProcessHandle` / `DbgGetThreadHandle` | Return `mach_port_t` under a typedef |
| `GuiGetWindowHandle` | Opaque handle |

### SDK

A portability header defines `duint`, `BYTE`, `ULONG_PTR`, `HANDLE` and `HWND`. MSVC CRT shims
(`strncpy_s` and friends) serve Tier 2. `#pragma comment(lib, ...)` is replaced by a CMake
package, `machdbg-pluginsdk`. `PLUG_SDKVERSION` is bumped, and `PLUG_PLATFORM_MACOS` lets a
plugin detect the platform.

### Tiers

- **Tier 1** — recompiles unmodified. Bridge, plugin and expression APIs only, no Windows types.
- **Tier 2** — recompiles against the portability header and CRT shims.
- **Tier 3** — architecture-bound. Compiles, but only meaningful on one architecture.
- **Tier 4** — cannot port: PEB/TEB, SEH, PE structures, Win32 UI, direct Windows APIs.

### Architecture declaration

The brief offers two answers for x86-only plugins on Apple Silicon: refuse to load, or ship an
arm64 sibling. Both push the problem onto the plugin author. Instead, the SDK exposes hardware
breakpoints through the architecture-neutral API of §5, and each plugin declares the
architectures it supports through a manifest export. The loader checks the declaration and
refuses with a named message rather than crashing against a register that does not exist. A
BVR/BCR-based sibling remains possible without a new SDK.

### Licence

x64dbg's plugin exception is carried into the macOS SDK verbatim. Plugins may remain
closed-source, commercial or private unless they copy code from machdbg or x64dbg. That right
must not disappear in the crossing.

### Acceptance test

`mrexodia/StackContains`, compiled from unmodified source, loading, and `stack.contains`
evaluating in the expression parser.

## 11. Testing

Three layers, and only the middle one needs privilege.

**Unit.** The Mach-O parser fed from on-disk fixtures — the byte-reader interface of §8 pays for
itself here — plus the token emitter, the fixup parser and the vendored expression parser tests.
No process, no signing, runs on any runner.

**Engine integration.** Small C targets mirroring `ElfBug/tests/targets/*`, launched under
MachBug. This section originally claimed CI would have to run engine tests as root, on the
premise that `task_for_pid` needs an Apple-issued certificate that CI does not have. That premise
was wrong (see §9's correction, measured in milestone 2 task 1): a target the test suite builds
and ad-hoc signs with `com.apple.security.get-task-allow`
(`src/cross/MachBug/tests/targets.entitlements`) is reachable by an ordinary, unprivileged,
ad-hoc-signed caller with no debugger entitlement at all. No root, and no Apple-issued identity,
is needed for our own fixtures. CI proves this directly: `.github/workflows/macos.yml` builds and
runs `MachBug_tests` as the default runner user.

**GUI smoke.** Launch the `.app` and capture a screenshot. This is the milestone 1 proof.

### The Intel coverage problem

The project owner has no Intel Mac, and D4 commits to universal support. GitHub Actions provides
an Intel macOS runner image alongside the arm64 images, which gives the x86-64 half of
milestones 3, 4 and 6 real coverage without buying hardware.

Confirmed at milestone 1, ahead of schedule, rather than deferred to milestone 3: the
`.github/workflows/macos.yml` Intel job ran on `macos-15-intel` on 2026-09-08 and reported
`x86_64` / macOS 15.7.9 (build 24G830). The runner is real and the label works. The contingency
this section anticipated — falling back to the `unverified` label because the image was gone —
did not materialise; the `macos-13` label named in the original plan no longer exists, but its
retirement took the label with it, not the architecture. Rosetta is not a substitute; it remains
out of scope. The image's continued availability still needs a fresh check before milestones 3,
4 and 6 build anything on it, since this job only reports and does not compile.

## 12. Milestones and definition of done

| # | Milestone | Proof |
|---|---|---|
| 0 | Upstream alignment: read `src/cross`, vendor-and-strip, agree the API shape, licences and credits | This document, an agreed API header, `CREDITS.md`, `docs/licenses.md` |
| 1 | Widgets build and run on macOS under Qt 6; `hex_viewer` and `minidump` launch as `.app` | Screenshots, CI job |
| 2 | MachBug skeleton: launch, attach, exception loop, stop/resume, signing working | Engine test harness |
| 3 | Registers and memory read/write, both architectures | Register view populated on a real process — `regview.app`, checked by `regview --selftest` offscreen and by the engine suite on both architectures |
| 4 | Software and hardware breakpoints, single-step, both architectures | Test targets |
| 5 | Mach-O module enumeration via dyld, memory map, thread list | Views populated |
| 6 | Capstone behind the disassembly interface, both architectures | Disassembly view on native code |
| 7 | Symbols: nlist plus DWARF/dSYM | Symbol view, source view |
| 8 | Plugin loader and macOS SDK | StackContains evaluates `stack.contains` |
| 9 | Database, analysis, graph, script engine | End-to-end session on a sample app |
| 10 | Packaging: universal binary, entitlements, signing script, distribution docs | Documented reproducible build on a clean Mac |

### Definition of done for v1

- Universal `.app` for arm64 and x86-64, building from one documented command.
- A supplied signing script that produces a working debugger from the user's own identity.
  (This replaces the original goal of a signed, notarised `.dmg`, per D8.)
- Attaches to and launches a user-built native Mach-O process on both architectures.
- Breakpoints, stepping, register and memory editing, memory map, threads and modules all work.
- The debuggability report answers correctly in all four cases of §9.
- The read-only fixup inspector displays stub resolution for a loaded module.
- A Tier 1 plugin compiled from unmodified source loads and runs.
- x86-64 behaviour is either proven on an Intel CI runner or labelled `unverified`. **Proven for the engine as of milestone 3**: the `macos-15-intel` job builds and runs the whole engine suite as the ordinary user, and reported `All tests passed (468 assertions in 48 test cases)` on x86_64, macOS 15.7.9 (2026-09-09). That covers registers, memory, the exception loop and the attach path; the Qt front end is still built and smoke-launched only on Apple Silicon.
- Credits and licences complete.

## 13. Open research questions

Each of these gets its own issue. None blocks work before the milestone named.

1. **AArch64 assembler** (milestone 6): does asmtk parse AArch64? If not, LLVM MC as an
   assembler only, or the Clang integrated assembler. Keystone is excluded on licence grounds.
2. **DWARF parser** (milestone 7): libdwarf pending a licence check, or an alternative.
3. **Intel CI image** — answered 2026-09-08 in milestone 1's CI workflow, and put to work in
   milestone 3: the image is available as `macos-15-intel` (the `macos-13` label named when this
   question was written no longer exists), and as of milestone 3 task 8 that job builds and runs
   the engine tests there rather than only reporting `uname`. Its `continue-on-error` is gone with
   that change: a job carrying the only proof of the x86-64 half has to be able to fail. The job in `.github/workflows/macos.yml` ran `uname -m` and `sw_vers` on it
   and got `x86_64` and macOS 15.7.9 (build 24G830) back, confirming a working Intel runner
   without requiring the project owner to buy Intel hardware. `unverified` remains the fallback
   label if a future milestone finds the image gone, but that has not happened.
4. **Capstone version and constants** (milestone 6): confirm the AArch64 architecture constant
   against the installed header.
5. **macOS bundling in `qt_executable`** (milestone 1): `Qt.cmake` already prefers Qt 6, so the
   original question — sizing Qt 5 to Qt 6 churn — is largely answered. What remains is giving
   `qt_executable` a macOS branch with `MACOSX_BUNDLE`, an `Info.plist`, and `macdeployqt` in
   place of `windeployqt`.

## 14. Licensing and credits

machdbg is a derivative work of x64dbg and is GPLv3. The source is published.

`docs/licenses.md` lists only what ships: machdbg (GPLv3), Qt 6 (LGPLv3), Capstone (BSD-3),
asmjit and asmtk (zlib), jansson (MIT), lz4 (BSD), yara (BSD-3), and the DWARF parser once
question 2 is resolved. TitanEngine, Zydis, XEDParse, GleeBug and Scylla leave the list because their code
leaves the tree.

`CREDITS.md` names, regardless of what code survives:

- x64dbg: mrexodia (Duncan Ogilvie), Sigma, tr4ceflow, Dreg, Nukem, Herz3h, torusrxxx, and the
  contributor list.
- Cross-platform groundwork: @3rdit (ElfBug, cross debugger), @eldarkg (Wine build).
- Upstream dependencies: TitanEngine Community Edition, Zydis, XEDParse, asmjit, Scylla, jansson,
  lz4, the bug icon by VisualPharm, interface icons by Fugue, website by tr4ceflow.
- Reference plugins: mrexodia/StackContains, mrexodia/DrDecode.
- macOS prior art: gdbinit by Pedro Vilaça (fG!), as a design reference for register and context
  display conventions. Its LICENSE must be checked before reusing any code or layout; the credit
  stands either way.

The fork carries a distinct name, so no trademark agreement with the x64dbg project is needed.
Attribution is "based on x64dbg", never a claim to the name.

## 15. Verification status

This document was first written from the project brief alone. The upstream tree has since been
read at `x64dbg/x64dbg` `development` commit `8794998`, dated 2026-09-06.

Verified against the source:

- `src/cross/cmake.toml` uses cmkr with a `linux-x64` condition, and gates only `ElfBug` and
  `debugger` behind it. `minidump`, `remote_table`, `release_notes` and `hex_viewer` are
  ungated, which is what makes milestone 1 reachable.
- `elfbug_api.h` is 100 lines with the entry points, callback set and thread-safety comments
  this document mirrors. Its `ElfBugRegisters` is the flat x86-64 layout §5 replaces, and it has
  no attach entry point — only `ElfBugInit(path)`.
- `ElfBug/tests` provides `TestHarness.h`, `SymbolHelper.h` and six target programs, the shape
  milestone 2's harness copies.
- `widgets/Qt.cmake` resolves Qt 6 ahead of Qt 5 and has no macOS bundling.

Corrected as a result, with the original claims struck: `src/gui` is not deletable (§4), Zydis
is not removable before milestone 6 (§4, D5), and the widget library does not yet contain the
memory map, thread, symbol, graph or stack views (§3).

Still unverified, and to be checked before the milestone that depends on each: the 272
`BRIDGE_IMPEXP` exports and 39 `PLUG_IMPEXP` functions quoted from the brief, the internals of
`plugin_loader.cpp`, and every Apple API detail in §6 through §9 — those are stated from
documentation and must be checked against the installed SDK headers before use.
