# Building machdbg on macOS

## Requirements

| Tool | Minimum | Install |
|---|---|---|
| Xcode command line tools | current | `xcode-select --install` |
| CMake | 3.19 | `brew install cmake` |
| Ninja | 1.10 | `brew install ninja` |
| cmkr | current | [GitHub release](https://github.com/build-cpp/cmkr/releases) (`cmkr-macos.zip`); not packaged by Homebrew |
| Capstone | 5 | `brew install capstone` — linked by the build as of milestone 6, not merely checked for |
| Qt | 6 | Official Qt online installer |

Capstone is found through `pkg-config`. If configuring fails with
`capstone/capstone.h: No such file or directory`, the cause is Capstone's own `.pc` file, which
reports its include directory as `<prefix>/include/capstone` -- one level below where
`#include <capstone/capstone.h>` resolves. The build corrects that itself; a hand-written
`CAPSTONE_CFLAGS` override has to do the same.

Qt does not come from Homebrew. Homebrew builds Qt for one architecture, and machdbg ships a
universal binary for arm64 and x86-64, which needs the universal Qt from the official
installer.

Point the build at it:

```bash
export QT_ROOT_DIR="$HOME/Qt/6.9.0/macos"
```

### Homebrew Qt (arm64-only development builds)

For a quick `macos-arm64` development build on Apple Silicon, `brew install qt` (Qt 6) is an
acceptable shortcut: point `QT_ROOT_DIR` at the Homebrew prefix instead of `$HOME/Qt/...`:

```bash
export QT_ROOT_DIR="$(brew --prefix qt)"
```

This Qt is single-architecture (matching whatever CPU Homebrew built it for) and **cannot**
satisfy the `macos-universal` preset, which links against both arm64 and x86-64 Qt libraries.
Use the official installer whenever you need `macos-universal`, and for anything you intend to
package or release.

#### Deployment target

The `CMAKE_OSX_DEPLOYMENT_TARGET` in `src/cross/CMakePresets.json` is set to match the minimum
macOS version that the installed Qt was built against, not chosen freely. Homebrew Qt 6.11's
`qtbase` is built for macOS 14.0. Two separate Homebrew formulae, `qtsvg` and `qtwebsockets`, are
bottled against macOS 26.0 (built on a newer runner); every link of the four Qt targets warns
about these frameworks. The floor deliberately tracks `qtbase` at 14.0 because raising it to 26.0
would set the bundles' minimum OS to 26 in a later milestone. Installing the official universal
Qt, required for `macos-universal` and eventual release, resolves this inconsistency at the source.

`scripts/check-bundles.sh` holds both halves of this to account. It **fails** when a binary this
project builds disagrees with the preset's floor, or when a bundle's `LSMinimumSystemVersion`
disagrees with what its own executable needs — a bundle that promises a lower floor than it can
honour starts on a machine it cannot run on, and the only symptom is a crash on an older macOS.
It **reports**, without failing, the highest floor among the Qt copies `macdeployqt` brings in:
those come from Homebrew bottles and are not ours to recompile, so today's bundles declare 14.0
while shipping libraries that need up to 26.0. That note is expected to go away with the official
Qt, not to be silenced.

### cmkr

`cmkr` is not a Homebrew formula. Download the `cmkr-macos.zip` asset from the
[latest release](https://github.com/build-cpp/cmkr/releases/latest), unzip it, and place the
`cmkr` binary somewhere on `PATH` (for example `/opt/homebrew/bin`):

```bash
mkdir -p /tmp/cmkr && cd /tmp/cmkr
gh release download --repo build-cpp/cmkr --pattern 'cmkr-macos.zip'
unzip cmkr-macos.zip
chmod +x cmkr
cp cmkr /opt/homebrew/bin/cmkr
```

## Checking the toolchain

```bash
./scripts/check-toolchain.sh
```

Every line reads `ok` when the machine is ready. A `FAIL` line names the tool and how to
install it.

## Building

The presets live at `src/cross/CMakePresets.json`, not the repository root — there is no root
`CMakeLists.txt` yet (milestone 1 introduces one, at which point these commands move to the
repository root). Run them from `src/cross`:

```bash
cd src/cross
cmake --preset macos-arm64
cmake --build --preset macos-arm64
```

`macos-arm64` builds for the host only and is the fast option for development.
`macos-universal` builds both architectures and is what releases use.

## Running the tests

`MachBug_tests` is off by default. Turn it on by re-configuring with
`-DMACHBUG_BUILD_TESTS=ON`, build the target explicitly, then run the binary:

```bash
cd src/cross
export QT_ROOT_DIR="$(brew --prefix qt)"
cmake --preset macos-arm64 -DMACHBUG_BUILD_TESTS=ON
cmake --build build/macos-arm64 --target MachBug_tests
./build/macos-arm64/tests/MachBug_tests
```

Milestone 6 adds a second suite, `machdbg_disasm_tests`, for the disassembler and the tokenizer.
It is separate because it links Qt and the widgets library, while `MachBug_tests` deliberately
links neither -- the engine is testable without a GUI toolkit and is kept that way:

```bash
cmake --build build/macos-arm64 --target machdbg_disasm_tests
./build/macos-arm64/tests/disasm/machdbg_disasm_tests
```

Both architectures are decoded here whatever the host is: Capstone opens a handle per
architecture, so the x86-64 cases run on Apple Silicon too. That is the only place x86-64
decoding is checked -- the Intel CI job builds and runs `MachBug_tests` alone and never builds a
Qt target.

### Why the test fixtures are signed, and why the tests need no root

The engine tests are a different case from the distribution signing described under "Signing"
below: they debug small programs `MachBug_tests` builds and launches itself
(`src/cross/MachBug/tests/targets/`), not an installed copy of machdbg. Each of those fixtures is ad-hoc signed with the
`com.apple.security.get-task-allow` entitlement as a build step (see the `codesign` custom
command in `MachBug/tests/cmake.toml`) -- that is the entitlement `task_for_pid` actually checks
for a target that opted in, and it is unrelated to `com.apple.security.cs.debugger`, which only
matters for reaching a target that did *not* opt in.

Milestone 0's plan assumed this would need root, on the premise that only an Apple-issued
identity with the debugger entitlement could call `task_for_pid` at all. That was measured and
found false: a same-user caller with no debugger entitlement of its own reaches a target signed
with `get-task-allow` and is denied (`KERN_FAILURE`) against one that lacks it -- root changes
neither outcome. Run the tests as your ordinary user; no `sudo`, and CI does not use it either
(see the comment next to the test step in `.github/workflows/macos.yml`).

## Seeing a target's registers and memory

`regview` is milestone 3's proof, extended by milestone 4: it launches or attaches to a target,
stops it, shows its registers and the memory around its program counter, and carries a breakpoint
bench (see below). It is a sample app in the shape of milestone 1's four, not the debugger -- the
debugger's own window needs disassembly and modules from later milestones, so it stays Linux-only
until those exist.

```bash
cd src/cross
cmake --preset macos-arm64 -DMACHBUG_BUILD_TESTS=ON
cmake --build build/macos-arm64 --target regview MachBug_tests
./build/macos-arm64/regview.app/Contents/MacOS/regview \
    build/macos-arm64/tests/targets/run_endlessly
```

Any target works, not only the test fixtures, as long as it is signed with
`com.apple.security.get-task-allow` (see the note above on why).

### Checking it without looking at it

`--selftest` runs the whole app against a real target, reports what the register table and the
memory panel are actually showing, and exits non-zero if either came back empty. It needs no
window server, which is what makes it the check CI can run:

```bash
QT_QPA_PLATFORM=offscreen ./build/macos-arm64/regview.app/Contents/MacOS/regview \
    --selftest build/macos-arm64/tests/targets/run_endlessly
```

A passing run prints every register row, the memory panel's base and size, and
`RESULT: the register table and the memory panel are populated`. The check is deliberately about
the *values*: a table of 34 rows of zeroes is what a view that renders but is never fed looks
like, and a row count alone would accept it.

### Disassembly

Milestone 6 adds a disassembly tab, decoded by Capstone and fed from the target's own memory at
the stopped thread's program counter.

Two things about it are worth knowing, because both were found the hard way:

- **The view paints nothing unless a memory provider is installed.** `Disassembly` calls
  `setDrawDebugOnly(true)`, and this port's `DbgIsDebugging()` answers "is a provider
  installed". `regview` installs one when it launches a target and removes it when the target
  goes away.
- **The architecture comes from the engine, not from the machine.** Decoding arm64 bytes as
  x86-64 does not fail -- it invents plausible instructions -- so `--selftest` checks that the
  instruction at the program counter is four bytes long on arm64, not merely that it is not
  empty.

### Modules, the memory map and threads

Milestone 5 adds three more tables, in a tabbed panel beside the breakpoint bench: the modules
dyld has loaded (with each one's ASLR slide, which is what turns a link-time address into a
runtime one), the memory map (every region's protections, its tag as a readable name, and the
module it belongs to), and the threads (id, name, state, program counter, and which one the
current stop is on).

Two things about those tables are properties of the platform rather than of the app, and are
worth knowing before they look like bugs:

- **At the target's first stop the module table is empty** and every memory region reports no
  module. That stop is dyld's own notification trap, before it has published anything. The
  tables fill in at the next stop, which is why `--selftest` resumes the target once before
  asserting anything.
- **The state column is the kernel's, and it does not mean "stopped".** A thread parked in an
  unanswered exception reply reads as `waiting`, and so does every thread of a target that
  Pause suspended. The separate "Stopped" column is the engine's own answer, which is the only
  one that means what it says.

`--selftest` now checks all five views, not just two: it fails unless a module reports a
non-zero slide, a memory region names the module it belongs to, and a thread reports a readable
program counter.

### The breakpoint bench

Since milestone 4, `regview` also carries a breakpoint list: an address, a kind (software,
hardware exec, hardware read, hardware write), a size for the watchpoint kinds, and a state
column that says whether the *target* is carrying each one rather than whether the list
remembers it. Double-clicking a row switches it off and on. A stop names the address in the
status bar, and a watchpoint stop names it as a data address, which is what it is -- the
program counter at a watchpoint hit belongs to the instruction that made the access.

`--selftest-breakpoint` is the half of that a job can check. It sets a breakpoint on the address
the fixture publishes, resumes, and exits non-zero unless the target stops there with the list
showing it armed:

```bash
QT_QPA_PLATFORM=offscreen ./build/macos-arm64/regview.app/Contents/MacOS/regview \
    --selftest-breakpoint build/macos-arm64/tests/targets/known_function
```

It is a separate flag from `--selftest` on purpose: it needs a target that publishes an address,
and a check that quietly skips itself when pointed at anything else is a check that passes for
the wrong reason. Both run in CI.

`src/cross/tests/accessibility/regview_accessibility.py` checks the same thing through the native
accessibility API, which is what a screen reader sees. It currently **fails** -- not because the
view is empty, but because a table repopulated after its accessible interface exists reads as
having no rows (issue #88). The script is the reproduction for that bug; `--selftest` is what
proves the view meanwhile.

## Signing

macOS refuses to let an unsigned binary debug anything: `task_for_pid` requires the
`com.apple.security.cs.debugger` entitlement, which is only honoured on a binary signed with an
Apple-issued identity. That is a consequence for distribution, not for the build: per decision
D8, machdbg ships unsigned, and signing is a post-install step the user runs themselves before
first launch. See `packaging/sign.sh` once milestone 10 lands.
