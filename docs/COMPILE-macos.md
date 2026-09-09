# Building machdbg on macOS

## Requirements

| Tool | Minimum | Install |
|---|---|---|
| Xcode command line tools | current | `xcode-select --install` |
| CMake | 3.19 | `brew install cmake` |
| Ninja | 1.10 | `brew install ninja` |
| cmkr | current | [GitHub release](https://github.com/build-cpp/cmkr/releases) (`cmkr-macos.zip`); not packaged by Homebrew |
| Capstone | 5 | `brew install capstone` |
| Qt | 6 | Official Qt online installer |

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

## Seeing a target's registers and memory

`regview` is milestone 3's proof: it launches or attaches to a target, stops it, and shows its
registers and the memory around its program counter. It is a sample app in the shape of milestone
1's four, not the debugger -- the debugger's own window needs disassembly, breakpoints and modules
from later milestones, so it stays Linux-only until those exist.

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
