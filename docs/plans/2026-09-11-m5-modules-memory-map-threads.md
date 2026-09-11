# Milestone 5 — Modules, Memory Map and Threads Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Answer "what is loaded, where, and what is this address part of" for a live target — modules with their load addresses and slides, a memory map with readable tags, and a thread list with names and states — on both architectures.

**Architecture:** One Mach-O parser that knows nothing about Mach. It reads through a byte-reader interface with two implementations, a file on disk and the target's memory, because system dylibs have no file to open (measured below). Above it, `core/Modules` enumerates through `task_info(TASK_DYLD_INFO)` and diffs at each stop, exactly as milestone 4's `core/Threads` does — the shape is deliberately the same, because the reason is the same: macOS notifies a debugger of neither event. The memory map grows out of milestone 3's `memory::EnumRegions` rather than beside it, and thread details grow out of milestone 4's `Threads`.

**Tech Stack:** C++17, `task_info(TASK_DYLD_INFO)`, `mach_vm_read_overwrite` (milestone 3's `MachBug::memory`), `mach_vm_region_recurse`, `thread_info` with `THREAD_BASIC_INFO` and `THREAD_EXTENDED_INFO`, Qt 6 with the vendored `StdTable`, Catch2 v3.14.0, cmkr.

**Spec:** `docs/specs/2026-09-07-macos-port-design.md` — section 5 (the C API's module and thread callbacks), section 8 (modules, memory map, threads, and the layering requirement), and section 12's milestone 5 row.

**Predecessors:** milestones 0 through 4, complete and merged. The engine launches, attaches, stops, reads and writes registers and memory, sets software and hardware breakpoints and watchpoints, steps, and tracks threads by their stable system ids. `regview.app` shows registers, memory and a breakpoint bench, and two `--selftest` modes run in CI.

## Decisions taken before this plan was written

Settled with the project owner on 2026-09-11. Do not reopen them mid-task.

1. **The parser reads only what this milestone's views need**: the header, `LC_SEGMENT_64` and `LC_UUID`. `LC_SYMTAB`, `LC_DYSYMTAB`, `LC_FUNCTION_STARTS` and the chained fixups are milestone 6 and 7 work. The byte-reader interface is designed so those can be added without reshaping anything, but code nothing consumes yet is code nothing tests yet.
2. **Load and unload are discovered by polling at each stop, with a diff** — the shape milestone 4 proved for threads — not by an internal breakpoint on dyld's notification function. The spec's section 8 describes the breakpoint, and milestone 4 made it possible; it is deferred because a debugger acts at stops anyway, and an internal breakpoint the user did not set has to be invisible in the list, invisible in the hardware slot count, and restored around every detach. Revisit when something needs module events while the target runs.
3. **All three views ship**: modules, memory map and threads, each a `StdTable` subclass in `views/`, each extending `--selftest`. The milestone's proof in section 12 is "views populated", and the marginal cost of each table once the engine side exists is small.
4. **The memory map expands `MemEnumRegions`** rather than adding a second entry beside it. One way to ask a question is worth more than a preserved signature; the vtable is not yet shipped to anyone outside this repository.

## What was verified before this plan was written

Measured on this machine (Apple Silicon, macOS 26.x, SDK 26.5) on 2026-09-11 with a throwaway program. Do not re-derive.

- **`task_info(TASK_DYLD_INFO)` answers for the calling task and returns `all_image_info_format = 1`** (`TASK_DYLD_ALL_IMAGE_INFO_64`). `TASK_DYLD_INFO` is `17`; the reply's `all_image_info_addr` is an address **in the target's address space**, so everything below it is read with `memory::Read`, never dereferenced.
- **`dyld_all_image_infos` layout, which is what gets read out of the target**: `version` at offset 0 (`uint32_t`), `infoArrayCount` at 4 (`uint32_t`), `infoArray` at 8 (pointer), `notification` at 16 (pointer). The struct is 368 bytes at version 17, and its size grows with the version — read the four fields by offset, never `sizeof`.
- **`dyld_image_info` is 24 bytes**: `imageLoadAddress` at 0, `imageFilePath` at 8, `imageFileModDate` at 16. The path is a pointer into the target, so it is a second read, and it is NUL-terminated with no length available — read it in bounded chunks.
- **`infoArray` is documented as NULL while dyld is modifying it** (`mach-o/dyld_images.h:52`). A read that finds NULL is "come back at the next stop", not an error.
- **A trivial program loads 45 images**, so a fixed-size array is not an option for enumeration and the C API's `capacity`/`count` shape (already used by `MemEnumRegions`) is the right one.
- **System dylibs have no file on disk.** `ls /usr/lib/libc++.1.dylib` and `ls /usr/lib/libSystem.B.dylib` both report "No such file or directory", while both appear in `infoArray` with real load addresses. This is the measurement behind the whole layering decision: a parser that opens the path fails on the first system library, which is every process.
- **`THREAD_EXTENDED_INFO` is flavor `5`**, and its `pth_name` (`MAXTHREADNAMESIZE`) and `pth_run_state` are the name and state to report. `pthread_getname_np` cannot be used: it reads the calling process's own threads.
- **`MachBug::memory::Read` already does bounded cross-process reads** and `EnumRegions` already reports `base`, `size`, current and maximum protection and the user tag, using `mach_vm_region` with `VM_REGION_BASIC_INFO_64` and `VM_REGION_EXTENDED_INFO`. Task 4 changes how it walks, not what it fundamentally does.
- **`core/Threads` already holds one send right per live thread, keyed by `THREAD_IDENTIFIER_INFO.thread_id`**, and diffs at each stop. Task 5 adds detail to what it reports; it does not re-enumerate.

**Left to measure, by the task that needs it:** whether `mach_vm_region_recurse` reports a target's own `__TEXT` with a user tag that distinguishes it from an anonymous mapping (Task 4 Step 2), and what `pth_run_state` reads as for a thread parked in an unanswered exception reply, which is the state this engine's own stops create (Task 5 Step 2).

## Global Constraints

- **The six verification scripts must stay green**: `check-toolchain.sh`, `verify-vendor.sh`, `check-credits.sh`, `check-bundles.sh`, `smoke-launch.sh`, `check-build-warnings.sh`.
- **Anything authored inside `src/` or `cmake/` must be added to `verify-vendor.sh`'s `required[]` in the same commit**, or a future re-vendor deletes it silently.
- **New CMake source files need `cmkr gen` in their own directory** before they build, and a new test fixture needs both its `[target.machbug_fixture_*]` block and an entry in the `add_dependencies(MachBug_tests ...)` list, or `--target MachBug_tests` will not build it and the test will not find it.
- **CI must stay green on both architectures.** The Intel job is not `continue-on-error`.
- **One GitHub issue per task, assigned to `jacksonmafra-umain`; every change lands through a pull request.** No direct pushes to `main`.
- **Nothing in the C API grows a pointer whose lifetime a caller has to reason about.** `DbgModule` and `DbgThread` carry inline buffers, like `DbgMemoryRegion` carries inline scalars: the caller supplies an array, the engine fills it, and nothing has to be freed.

---

## Task 1: The Mach-O reader and image parser

**Files:**
- Create: `src/cross/MachBug/MachBug/macho/Reader.h`, `macho/FileReader.cpp`, `macho/MemoryReader.cpp`, `macho/Image.h`, `macho/Image.cpp`
- Modify: `src/cross/MachBug/MachBug/cmake.toml`, `scripts/verify-vendor.sh`
- Test: `src/cross/MachBug/tests/macho_image.cpp` (create), `tests/cmake.toml`

**Interfaces:**
- Produces: `macho::Reader` (an interface with `bool Read(uint64_t address, void* out, uint64_t size)`), `macho::FileReader`, `macho::MemoryReader(task)`, and `macho::Image::Parse(Reader&, uint64_t loadAddress, Image* out, std::string* error)` filling `Image{ std::vector<Segment> segments; uint8_t uuid[16]; bool hasUuid; uint64_t textVmAddr; }` with `Segment{ char name[16]; uint64_t vmAddr; uint64_t vmSize; uint32_t initProt; uint32_t maxProt; }`.

- [ ] **Step 1: Open the issue** — stating the layering requirement and the measurement behind it: system dylibs have no file on disk, so a parser that opens a path fails on the first one, and the byte-reader is the shape that survives that.

- [ ] **Step 2: Write the failing test** — three cases in `macho_image.cpp`: the test binary's own file on disk parses and reports a `__TEXT` segment; the same binary parsed out of its own live memory through `MemoryReader` against `mach_task_self()` reports the same segment names; and a path that is not Mach-O is refused with a diagnostic rather than accepted with zero segments.

- [ ] **Step 3: Run it and watch it fail to compile** — the header does not exist.

- [ ] **Step 4: Write `Reader.h` and the two readers.** `FileReader` holds an open `std::ifstream` and treats `address` as a file offset; `MemoryReader` holds a task port and calls `memory::Read`. Nothing else in the interface: a reader that can also seek, or report a size, is a reader two implementations disagree about.

- [ ] **Step 5: Write `Image::Parse`.** Read `mach_header_64`, refuse anything whose magic is not `MH_MAGIC_64` by name, then walk `ncmds` load commands reading each `load_command` header first and skipping by `cmdsize`. Handle `LC_SEGMENT_64` and `LC_UUID`; ignore the rest. **Bound the walk**: a corrupt `cmdsize` of 0 is an infinite loop, and a `sizeofcmds` that runs past what the reader will return is a target lying to a debugger, which is the normal case for a debugger.

- [ ] **Step 6: Run the tests and make them pass.**

- [ ] **Step 7: Add a system dylib case.** Parse `/usr/lib/libc++.1.dylib` out of this process's own memory, found through `_dyld_get_image_header`. It is the case the design exists for, and it must not be parseable from disk — assert that opening the path fails, so the test says out loud why the memory path is not optional.

- [ ] **Step 8: Commit.**

---

## Task 2: Enumerating what dyld has loaded

**Files:**
- Create: `src/cross/MachBug/MachBug/core/Modules.h`, `core/Modules.cpp`
- Test: `src/cross/MachBug/tests/modules.cpp` (create)

**Interfaces:**
- Consumes: `macho::MemoryReader`, `macho::Image` (Task 1), `memory::Read`.
- Produces: `Modules::Image{ uint64_t loadAddress; uint64_t slide; uint64_t size; std::string path; }`, `Modules::Enumerate(mach_port_t task, std::vector<Image>* out, std::string* error)`.

- [ ] **Step 1: Open the issue** — with the layout measured above, and the note that `infoArray` being NULL means dyld is mid-update rather than that something failed.

- [ ] **Step 2: Write the failing test** — launch `run_endlessly`, enumerate, and require that the fixture's own path appears with a load address above 0x1000, that more than ten images are reported, and that at least one of them is a `/usr/lib/` path with no file on disk.

- [ ] **Step 3 to Step 6** — `task_info(TASK_DYLD_INFO)`, read the four fields by offset, read `infoArrayCount` entries of 24 bytes, read each path in bounded chunks until a NUL. The slide is the load address minus the parsed `__TEXT` `vmaddr`; the size is the sum of the segments' `vmsize`, which is what a memory map has to correlate against. Commit.

- [ ] **Step 7: Assert the slide is real** — for the main executable, `loadAddress - slide` must equal the `__TEXT` vmaddr the parser read, and with ASLR on, the slide must not be zero. A slide of zero everywhere is what a stub that forgot to subtract looks like.

---

## Task 3: Noticing what loads and unloads

**Files:**
- Modify: `core/Debugger.h`, `core/Debugger.Loop.cpp`, `core/Modules.h`, `core/Modules.cpp`
- Test: `tests/modules.cpp`, `tests/TestHarness.h`

**Interfaces:**
- Produces: `Modules::Change{ appeared, disappeared }` from `Modules::Refresh(task)`, `Debugger::ModuleCount()`, `cbLoadModule(uint64_t base, const std::string& path)` / `cbUnloadModule(uint64_t base)`, and `EventType::LoadModule` / `UnloadModule` in the harness.

- [ ] **Step 1: Open the issue** — noting that this is milestone 4's thread-tracking shape reused deliberately, and that the first `Refresh` establishes a baseline and reports nothing, for the same reason: the images a target already had were not loaded under this engine's watch.

- [ ] **Step 2 to Step 6** — the diff keyed by load address, the call from `refreshThreads`'s sibling at every stop (rename that call site to something that says it refreshes both, or add a second call next to it — do not hide a module refresh inside a function named for threads), the callbacks, and a test that a target which `dlopen`s a library reports it. **A new fixture is needed**: `loads_a_library.cpp`, which `dlopen`s a system dylib after publishing a marker, so the test can stop the target before and after.

---

## Task 4: The memory map

**Files:**
- Modify: `memory/Memory.h`, `memory/Memory.cpp`, `api/machbug_api.h`, `api/machbug_api.cpp`
- Test: `tests/memory.cpp`

**Interfaces:**
- Produces: `memory::Region` gaining `const char* tagName` and `uint64_t moduleBase`; `DbgMemoryRegion` gaining the same two fields; `EnumRegions` walking with `mach_vm_region_recurse`.

- [ ] **Step 1: Open the issue.**

- [ ] **Step 2: Measure first** — print the tag, depth and protections `mach_vm_region_recurse` reports for a stopped target's `__TEXT`, its stack and its heap, and **record all three in the pull request**. The taxonomy is not Windows's, and the mapping from tag to name has to be written against what the kernel actually reports for these three, not against the `VM_MEMORY_*` list in a header.

- [ ] **Step 3 to Step 7** — the walk with submaps, the tag-to-name table covering at least `VM_MEMORY_MALLOC*`, `VM_MEMORY_STACK`, `VM_MEMORY_DYLIB`, `VM_MEMORY_DYLD*` and unmapped-to-`nullptr`, the correlation of each region against the module list from Task 2, the API fields, and tests that the region containing the program counter names the main executable's base.

---

## Task 5: What a thread is, beyond its id

**Files:**
- Modify: `core/Threads.h`, `core/Threads.cpp`, `api/machbug_api.h`, `api/machbug_api.cpp`
- Test: `tests/threads.cpp`

**Interfaces:**
- Produces: `Threads::Detail{ uint64_t id; mach_port_t port; std::string name; int32_t runState; uint64_t pc; }`, `Threads::Describe(uint64_t id, Detail* out)`, `DbgThread` with inline `char name[64]`, and `ThreadEnum(void* impl, DbgThread* out, uint32_t capacity, uint32_t* count)`.

- [ ] **Step 1: Open the issue.**

- [ ] **Step 2: Measure** — what `pth_run_state` reads as for a thread parked in an unanswered exception reply, which is the state every stop this engine creates produces. **Record it in the pull request**, and name the states against what was seen rather than against the header's list.

- [ ] **Step 3 to Step 6** — `THREAD_BASIC_INFO` and `THREAD_EXTENDED_INFO`, the program counter through `arch::Read` (which already strips pointer authentication on arm64, and must not be duplicated here), the API entry, and a test against `multi_threaded` that the main thread is distinguishable from the two it spawns.

---

## Task 6: The module and thread entries of the vtable

**Files:**
- Modify: `api/machbug_api.h`, `api/machbug_api.cpp`
- Test: `tests/api_contract.cpp`

**Interfaces:**
- Produces: `DbgModule{ uint64_t base; uint64_t size; uint64_t slide; char path[1024]; uint8_t uuid[16]; }`, `ModEnum`, working `ModBaseFromAddr` and `ModNameFromAddr`, and `onLoadModule`/`onUnloadModule` forwarded to the C callbacks.

- [ ] **Step 1: Open the issue** — noting that these are the last `DbgStatus_NotSupported` stubs in the whole vtable, and that after this task every entry either works or is refused for a reason that names the missing target rather than the missing feature.

- [ ] **Step 2 to Step 5** — the contract test that drives the whole path from C (launch, enumerate modules, find the one containing the program counter by address, read its name, and receive `onLoadModule` for a `dlopen`), the implementation with milestone 3's status vocabulary, and the run on both architectures.

---

## Task 7: The three views

**Files:**
- Create: `views/ModuleTable.{h,cpp}`, `views/MemoryMapTable.{h,cpp}`, `views/ThreadTable.{h,cpp}`
- Modify: `regview/MainWindow.{h,cpp}`, `regview/main.cpp`, `views/cmake.toml`, `scripts/verify-vendor.sh`, `.github/workflows/macos.yml`, `docs/COMPILE-macos.md`

- [ ] **Step 1: Open the issue** — the three tables, named for the accessibility tree as `RegisterTable` and `BreakpointTable` are, with #88's caveat unchanged: a repopulated table reads as empty to an accessibility client, so `--selftest` is what proves them.

- [ ] **Step 2 to Step 7** — the widgets, a tabbed panel so four tables and a hex dump still fit in one window, and the `--selftest` extension: the module list must contain the fixture's own path, the memory map must have a region naming the fixture's base, and the thread list must show at least one thread whose id resolves. Three assertions a job can run, in the run CI already does.

---

## Task 8: Documentation and the milestone's own record

**Files:** `docs/COMPILE-macos.md`, `docs/specs/2026-09-07-macos-port-design.md`

- [ ] **Step 1: Open the issue.**

- [ ] **Step 2 to Step 4** — record what was measured rather than what was planned: the `dyld_all_image_infos` layout and the version it was read at, the tags the memory map actually sees, the thread run state a stopped thread reports, and the x86-64 result from the Intel job. Where a measurement contradicts the spec, correct the spec and say it was corrected.

---

## Milestone 5 exit criteria

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

- A Mach-O image parses identically from a file on disk and from the same image in live memory, and a system dylib with no file on disk parses from memory alone.
- Enumerating a stopped target reports its own executable path, its real load address, and a non-zero ASLR slide.
- A target that `dlopen`s a library reports `onLoadModule` with the library's base and path at the next stop.
- Every memory region reports a readable tag name, and the region containing the program counter names the executable it belongs to.
- Each thread reports its id, its name where it has one, and its program counter.
- `ModBaseFromAddr` and `ModNameFromAddr` answer for an address inside a loaded module and refuse, by name, for one that is in no module.
- `regview --selftest` requires the module list, the memory map and the thread list to be populated, and CI runs it.
