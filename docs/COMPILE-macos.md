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

## Signing

macOS refuses to let an unsigned binary debug anything: `task_for_pid` requires the
`com.apple.security.cs.debugger` entitlement, which is only honoured on a binary signed with an
Apple-issued identity. That is a consequence for distribution, not for the build: per decision
D8, machdbg ships unsigned, and signing is a post-install step the user runs themselves before
first launch. See `packaging/sign.sh` once milestone 10 lands.

### Why the test fixtures are signed, and why the tests need no root

The engine tests above are a different case from distribution: they debug small programs
`MachBug_tests` builds and launches itself (`src/cross/MachBug/tests/targets/`), not an installed
copy of machdbg. Each of those fixtures is ad-hoc signed with the
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
