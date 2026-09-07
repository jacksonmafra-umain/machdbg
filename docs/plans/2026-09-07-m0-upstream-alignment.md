# Milestone 0 — Upstream Alignment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Put the vendored x64dbg tree on disk at a pinned upstream commit, stripped of what
cannot survive on macOS, configuring under CMake on Apple Silicon, with the engine contract
header and the licence paperwork in place.

**Architecture:** machdbg is an independent GPLv3 fork of x64dbg (decision D2 in the spec).
Upstream is copied in wholesale at a pinned commit and edited freely — no submodule, no patch
overlay. Milestone 0 produces no debugger behaviour; it produces a tree that configures, a
contract header that compiles, and a provenance record that makes future upstream merges
tractable.

**Tech Stack:** C++17/20, CMake 3.19+ driven by cmkr (`cmake.toml`), Qt 6, Catch2 v3.14.0,
Bash for the vendoring and verification scripts, `gh` for issues and pull requests.

**Spec:** `docs/specs/2026-09-07-macos-port-design.md`

## Global Constraints

Every task's requirements implicitly include this section.

- **Upstream pin:** `x64dbg/x64dbg`, branch `development`, commit `8794998` (2026-09-06). Every
  vendored file comes from this commit and no other.
- **Licence:** GPLv3. Every new source file is compatible with it. New dependencies are added to
  `docs/licenses.md` in the same commit that introduces them.
- **Workflow:** one GitHub issue per task, assigned to `jacksonmafra-umain`, labelled, and placed
  in milestone `M0 Upstream alignment`. One branch per task off `main`. One pull request per
  task. Nothing is pushed to `main` directly and nothing is merged without the owner asking.
- **Commits:** English, microcommits — one focused change each, self-contained. No assistant
  attribution of any kind in commit messages or pull request descriptions.
- **Code style:** DRY, clean-architecture boundaries where they fit. Comments are short and only
  where the code cannot explain itself.
- **CMake minimum:** 3.19, matching `src/cross/cmake.toml` upstream.
- **Catch2 version:** v3.14.0, matching `src/cross/ElfBug/tests/cmake.toml` upstream.
- **Build presets:** `macos-universal` targets `arm64;x86_64`. `macos-arm64` targets the host
  only and is what day-to-day work uses.
- **Starting a task:** `git checkout main && git pull --ff-only && git checkout -b <the branch
  named in that task's pull request step>`.
- **`#<issue>` in a pull request body** is the number `gh issue create` printed at the end of the
  URL in that same task's first step. Substitute it before running the command.

---

### Task 1: macOS toolchain check and build documentation

The repository currently documents no way to build anything, and the development machine has
neither CMake, Ninja, cmkr nor Qt. This task produces an executable answer to "do I have what I
need", plus the document that tells a newcomer how to get there.

**Files:**
- Create: `scripts/check-toolchain.sh`
- Create: `docs/COMPILE-macos.md`

**Interfaces:**
- Consumes: nothing.
- Produces: `scripts/check-toolchain.sh`, exit code 0 when the toolchain is complete and 1 when
  it is not, printing one line per tool in the form `ok <tool> <version>` or
  `missing <tool> — <install hint>`. Task 4 calls it before configuring.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Add a macOS toolchain check and build documentation" \
  --assignee jacksonmafra-umain --milestone "M0 Upstream alignment" \
  --label "type:chore,area:build,prio:high" \
  --body "Nothing in the repository states how to build on macOS, and no check exists for whether the required tools are installed.

Add scripts/check-toolchain.sh, which verifies cmake 3.19+, ninja, cmkr, capstone and a Qt 6 installation, printing one line per tool and exiting non-zero when any is missing. Add docs/COMPILE-macos.md documenting how to install each one, including why Qt 6 comes from the official installer rather than Homebrew: Homebrew ships a single-architecture Qt and the project builds universal binaries.

Done when the script passes on a machine with the toolchain installed and fails with actionable output on one without."
```

- [ ] **Step 2: Write the failing test**

Here the script is the test: it asserts the machine has the toolchain, and on this machine it
does not yet. Create `scripts/check-toolchain.sh`:

```bash
#!/usr/bin/env bash
# Verifies the macOS build toolchain. Exit 0 when complete, 1 when anything is missing.
set -uo pipefail

missing=0

require_version() {
    local tool="$1" min="$2" version_cmd="$3" hint="$4"
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'missing %s — %s\n' "$tool" "$hint"
        missing=1
        return
    fi
    local version
    version="$(eval "$version_cmd" 2>/dev/null | head -1)"
    if [[ -z "$version" ]]; then
        printf 'missing %s — installed but version unreadable; %s\n' "$tool" "$hint"
        missing=1
        return
    fi
    # Sorts the two versions and checks the minimum is not the greater of the pair.
    if [[ "$(printf '%s\n%s\n' "$min" "$version" | sort -V | head -1)" != "$min" ]]; then
        printf 'missing %s — found %s, need %s or newer; %s\n' "$tool" "$version" "$min" "$hint"
        missing=1
        return
    fi
    printf 'ok %s %s\n' "$tool" "$version"
}

require_present() {
    local tool="$1" hint="$2"
    if command -v "$tool" >/dev/null 2>&1; then
        printf 'ok %s present\n' "$tool"
    else
        printf 'missing %s — %s\n' "$tool" "$hint"
        missing=1
    fi
}

require_version cmake 3.19 "cmake --version | awk 'NR==1{print \$3}'" "brew install cmake"
require_version ninja 1.10 "ninja --version" "brew install ninja"
require_present cmkr "brew install cmkr"
require_present codesign "install the Xcode command line tools: xcode-select --install"

if [[ -z "${QT_ROOT_DIR:-}" ]]; then
    printf 'missing Qt6 — set QT_ROOT_DIR to a Qt 6 macOS installation; see docs/COMPILE-macos.md\n'
    missing=1
elif [[ ! -x "$QT_ROOT_DIR/bin/qmake" ]]; then
    printf 'missing Qt6 — QT_ROOT_DIR=%s has no bin/qmake\n' "$QT_ROOT_DIR"
    missing=1
else
    printf 'ok Qt6 %s\n' "$("$QT_ROOT_DIR/bin/qmake" -query QT_VERSION)"
fi

exit "$missing"
```

- [ ] **Step 3: Run it to make sure it fails**

Run: `chmod +x scripts/check-toolchain.sh && ./scripts/check-toolchain.sh; echo "exit=$?"`
Expected: exit=1, with `missing cmake`, `missing ninja`, `missing cmkr` and `missing Qt6` lines.
`ok codesign present` should already appear.

- [ ] **Step 4: Install the toolchain**

```bash
brew install cmake ninja cmkr capstone
```

Qt 6 is not installed by Homebrew here. Download the official Qt online installer from
`https://www.qt.io/download-qt-installer`, install Qt 6 for macOS, and export the root, for
example:

```bash
export QT_ROOT_DIR="$HOME/Qt/6.9.0/macos"
```

- [ ] **Step 5: Run it to make sure it passes**

Run: `./scripts/check-toolchain.sh; echo "exit=$?"`
Expected: exit=0, one `ok` line per tool including `ok Qt6 6.x.y`.

- [ ] **Step 6: Write the build documentation**

Create `docs/COMPILE-macos.md`:

````markdown
# Building machdbg on macOS

## Requirements

| Tool | Minimum | Install |
|---|---|---|
| Xcode command line tools | current | `xcode-select --install` |
| CMake | 3.19 | `brew install cmake` |
| Ninja | 1.10 | `brew install ninja` |
| cmkr | current | `brew install cmkr` |
| Capstone | 5 | `brew install capstone` |
| Qt | 6 | Official Qt online installer |

Qt does not come from Homebrew. Homebrew builds Qt for one architecture, and machdbg ships a
universal binary for arm64 and x86-64, which needs the universal Qt from the official
installer.

Point the build at it:

```bash
export QT_ROOT_DIR="$HOME/Qt/6.9.0/macos"
```

## Checking the toolchain

```bash
./scripts/check-toolchain.sh
```

Every line reads `ok` when the machine is ready. A `missing` line names the tool and how to
install it.

## Building

```bash
cmake --preset macos-arm64
cmake --build --preset macos-arm64
```

`macos-arm64` builds for the host only and is the fast option for development.
`macos-universal` builds both architectures and is what releases use.

## Signing

macOS refuses to let an unsigned binary debug anything: `task_for_pid` requires the
`com.apple.security.cs.debugger` entitlement, which is only honoured on a binary signed with an
Apple-issued identity. Signing is therefore a build step, not a distribution step. See
`packaging/sign.sh` once milestone 10 lands.
````

- [ ] **Step 7: Commit**

```bash
git add scripts/check-toolchain.sh docs/COMPILE-macos.md
git commit -m "Add a macOS toolchain check and build documentation"
```

- [ ] **Step 8: Open the pull request**

```bash
git push -u origin chore/macos-toolchain-check
gh pr create --repo jacksonmafra-umain/machdbg --base main \
  --assignee jacksonmafra-umain --milestone "M0 Upstream alignment" \
  --title "Add a macOS toolchain check and build documentation" \
  --body "Adds scripts/check-toolchain.sh and docs/COMPILE-macos.md.

The script verifies cmake 3.19+, ninja, cmkr, codesign and a Qt 6 installation found through QT_ROOT_DIR, printing one line per tool and exiting non-zero if any is missing. The documentation records why Qt comes from the official installer rather than Homebrew, and states that signing is a build requirement rather than a distribution step.

Closes #<issue>"
```

---

### Task 2: Vendor the upstream tree at a pinned commit

Decision D3 is vendor-and-strip. This task does the vendoring half and, more importantly,
records the provenance, because every future upstream merge depends on knowing exactly what was
copied and from where.

**Files:**
- Create: `scripts/vendor-upstream.sh`
- Create: `scripts/verify-vendor.sh`
- Create: `docs/upstream.md`
- Create: `src/` (the vendored tree, added by running the script)

**Interfaces:**
- Consumes: nothing.
- Produces: `UPSTREAM_COMMIT=8794998` recorded in `docs/upstream.md`; the vendored tree under
  `src/`; `scripts/verify-vendor.sh`, exit 0 when the tree matches the record. Task 3 extends
  `verify-vendor.sh` with the strip assertions.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Vendor the upstream x64dbg tree at commit 8794998" \
  --assignee jacksonmafra-umain --milestone "M0 Upstream alignment" \
  --label "type:chore,area:build,upstream,prio:high" \
  --body "Decision D3 in the design document is vendor-and-strip: copy the upstream tree at a pinned commit and edit it freely, rather than carrying a submodule or a patch overlay.

Add scripts/vendor-upstream.sh, which clones x64dbg/x64dbg at the pinned commit into a temporary directory and copies src/, cmake/ and the dependency manifests into place without the upstream .git directory. Add docs/upstream.md recording the pinned commit, the date, what was copied, what was deliberately not copied, and the procedure for re-syncing. Add scripts/verify-vendor.sh, which fails if the vendored tree is missing files the build needs.

Done when the tree is on disk, verify-vendor.sh passes, and the provenance is written down."
```

- [ ] **Step 2: Write the failing test**

Create `scripts/verify-vendor.sh`:

```bash
#!/usr/bin/env bash
# Verifies the vendored upstream tree is present and complete. Exit 0 when it is.
set -uo pipefail

cd "$(dirname "$0")/.."

failed=0

fail() { printf 'FAIL %s\n' "$1"; failed=1; }
pass() { printf 'ok %s\n' "$1"; }

# The pinned commit must be recorded, and it must be the one the design document names.
if [[ ! -f docs/upstream.md ]]; then
    fail "docs/upstream.md is missing"
elif ! grep -q '8794998' docs/upstream.md; then
    fail "docs/upstream.md does not record the pinned commit 8794998"
else
    pass "pinned commit recorded"
fi

# Files the build cannot do without.
required=(
    src/cross/cmake.toml
    src/cross/widgets/CMakeLists.txt
    src/cross/widgets/Qt.cmake
    src/cross/ElfBug/ElfBug/api/elfbug_api.h
    src/cross/ElfBug/tests/TestHarness.h
    src/bridge/bridgemain.h
    src/dbg/_plugins.h
    cmake/cmkr.cmake
)
for path in "${required[@]}"; do
    if [[ -e "$path" ]]; then
        pass "$path"
    else
        fail "$path is missing"
    fi
done

exit "$failed"
```

- [ ] **Step 3: Run it to make sure it fails**

Run: `chmod +x scripts/verify-vendor.sh && ./scripts/verify-vendor.sh; echo "exit=$?"`
Expected: exit=1, with a `FAIL` line for `docs/upstream.md` and one for every required path.

- [ ] **Step 4: Write the vendoring script**

Create `scripts/vendor-upstream.sh`:

```bash
#!/usr/bin/env bash
# Copies the upstream x64dbg tree at the pinned commit into this repository.
# Re-runnable: it overwrites the vendored paths and leaves machdbg-only files alone.
set -euo pipefail

UPSTREAM_URL="https://github.com/x64dbg/x64dbg.git"
UPSTREAM_BRANCH="development"
UPSTREAM_COMMIT="8794998"

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

git clone --branch "$UPSTREAM_BRANCH" "$UPSTREAM_URL" "$work/x64dbg"
git -C "$work/x64dbg" checkout "$UPSTREAM_COMMIT"

resolved="$(git -C "$work/x64dbg" rev-parse HEAD)"
printf 'vendoring %s at %s\n' "$UPSTREAM_URL" "$resolved"

for path in src cmake; do
    rm -rf "${repo_root:?}/$path"
    cp -R "$work/x64dbg/$path" "$repo_root/$path"
done

# Upstream build manifests the vendored tree references.
for file in cmake.toml CMakeLists.txt; do
    cp "$work/x64dbg/$file" "$repo_root/upstream-$file"
done

find "$repo_root/src" "$repo_root/cmake" -name '.git' -prune -exec rm -rf {} +

printf 'done. resolved commit: %s\n' "$resolved"
```

- [ ] **Step 5: Run the vendoring script**

Run: `chmod +x scripts/vendor-upstream.sh && ./scripts/vendor-upstream.sh`
Expected: it prints `vendoring ...` then `done. resolved commit: 8794998...`. `src/` and
`cmake/` now exist.

- [ ] **Step 6: Write the provenance document**

Create `docs/upstream.md`:

```markdown
# Upstream provenance

machdbg is a vendor-and-strip fork of [x64dbg](https://github.com/x64dbg/x64dbg), GPLv3.

| Field | Value |
|---|---|
| Repository | `https://github.com/x64dbg/x64dbg.git` |
| Branch | `development` |
| Pinned commit | `8794998` |
| Commit date | 2026-09-06 |
| Vendored on | 2026-09-07 |

## What was copied

`src/` and `cmake/` in full, plus the root `cmake.toml` and `CMakeLists.txt`, saved as
`upstream-cmake.toml` and `upstream-CMakeLists.txt` for reference rather than used directly.

The upstream `.git` directory is not copied. The vendored files are ordinary machdbg files from
this point on and are edited freely — that is the point of decision D3 in the design document.

## What was removed afterwards

See `scripts/strip-windows.sh` and the assertions in `scripts/verify-vendor.sh`.

## Re-syncing

1. Change `UPSTREAM_COMMIT` in `scripts/vendor-upstream.sh` and in the table above.
2. Run `./scripts/vendor-upstream.sh`. It overwrites the vendored paths.
3. Run `./scripts/strip-windows.sh` to reapply the removals.
4. Run `./scripts/verify-vendor.sh`.
5. Review the diff. Local changes to vendored files are overwritten by step 2, so the diff is
   the merge, and it is expected to be large.
```

- [ ] **Step 7: Run the test to verify it passes**

Run: `./scripts/verify-vendor.sh; echo "exit=$?"`
Expected: exit=0, every line `ok`.

- [ ] **Step 8: Commit**

Two commits, because the vendored tree is enormous and mixing it with authored code makes the
history unreadable.

```bash
git add scripts/vendor-upstream.sh scripts/verify-vendor.sh docs/upstream.md
git commit -m "Add upstream vendoring and verification scripts"

git add src cmake upstream-cmake.toml upstream-CMakeLists.txt
git commit -m "Vendor x64dbg at upstream commit 8794998"
```

- [ ] **Step 9: Open the pull request**

```bash
git push -u origin chore/vendor-upstream
gh pr create --repo jacksonmafra-umain/machdbg --base main \
  --assignee jacksonmafra-umain --milestone "M0 Upstream alignment" \
  --title "Vendor the upstream x64dbg tree at commit 8794998" \
  --body "Vendors src/ and cmake/ from x64dbg/x64dbg development at commit 8794998, with the provenance recorded in docs/upstream.md and a verification script that fails if the tree is missing anything the build needs.

The second commit is the vendored tree itself and is deliberately separate from the scripts, so the authored code stays reviewable.

Closes #<issue>"
```

---

### Task 3: Strip what cannot survive, without breaking the widget library

An earlier draft of the design document called for deleting `src/gui` and Zydis. Reading the
tree showed both are load-bearing: `src/cross/widgets/CMakeLists.txt` compiles 81 files out of
`src/gui/Src`, and `x64dbg_widgets` links `zydis_wrapper`. This task removes only what is
genuinely unreferenced, and adds the safety net that proves it.

**Files:**
- Create: `scripts/strip-windows.sh`
- Modify: `scripts/verify-vendor.sh`
- Delete: `src/dbg/TitanEngine/`, `src/dbg/GleeBug/`, `src/dbg/XEDParse/`,
  `src/dbg/DeviceNameResolver/`, `src/exe/`, `src/launcher/`, `src/loaddll/`

**Interfaces:**
- Consumes: the vendored tree and `scripts/verify-vendor.sh` from Task 2.
- Produces: `scripts/strip-windows.sh`; `verify-vendor.sh` gains two assertions —
  `assert_absent` for removed paths and a check that every file referenced by
  `widgets/CMakeLists.txt` still exists on disk.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Strip Windows-only components from the vendored tree" \
  --assignee jacksonmafra-umain --milestone "M0 Upstream alignment" \
  --label "type:chore,area:build,upstream,prio:high" \
  --body "Remove the vendored components that cannot survive on macOS: TitanEngine, GleeBug, XEDParse, DeviceNameResolver, and the Windows-only executable, launcher and loaddll projects.

Two things stay that the first draft of the design document proposed deleting. src/gui/Src stays, because widgets/CMakeLists.txt compiles 81 files out of it. Zydis stays until milestone 6, because x64dbg_widgets links zydis_wrapper and the replacement tokenizer does not exist yet.

Add scripts/strip-windows.sh to perform the removals idempotently, and extend scripts/verify-vendor.sh so it fails if a removed path reappears or if any file referenced by widgets/CMakeLists.txt is missing. That second assertion is the safety net: it is what would have caught the original mistake.

Done when the strip script runs, the verification passes, and re-running the strip script changes nothing."
```

- [ ] **Step 2: Write the failing test**

Append to `scripts/verify-vendor.sh`, before the final `exit`:

```bash
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
    done < <(grep -o 'widgets_SOURCE_DIR}/[^}"]*' "$widgets_cmake" | sed 's|widgets_SOURCE_DIR}/||')
    if [[ "$referenced" -lt 80 ]]; then
        fail "expected at least 80 referenced widget sources, found $referenced"
    else
        pass "$referenced widget sources referenced and present"
    fi
fi
```

- [ ] **Step 3: Run it to make sure it fails**

Run: `./scripts/verify-vendor.sh; echo "exit=$?"`
Expected: exit=1, with `FAIL src/dbg/TitanEngine should have been stripped` and one such line
per removed path. The widget assertion should already print
`ok <n> widget sources referenced and present`, because nothing has been deleted yet — that is
the point: the safety net passes before the strip and must still pass after it.

- [ ] **Step 4: Write the strip script**

Create `scripts/strip-windows.sh`:

```bash
#!/usr/bin/env bash
# Removes vendored components that cannot run on macOS. Idempotent.
#
# src/gui/Src stays: the widget library compiles 81 of its files.
# Zydis stays until milestone 6: x64dbg_widgets still links zydis_wrapper.
set -euo pipefail

cd "$(dirname "$0")/.."

paths=(
    src/dbg/TitanEngine
    src/dbg/GleeBug
    src/dbg/XEDParse
    src/dbg/DeviceNameResolver
    src/exe
    src/launcher
    src/loaddll
)

for path in "${paths[@]}"; do
    if [[ -e "$path" ]]; then
        rm -rf "$path"
        printf 'removed %s\n' "$path"
    else
        printf 'absent  %s\n' "$path"
    fi
done
```

- [ ] **Step 5: Run the strip script**

Run: `chmod +x scripts/strip-windows.sh && ./scripts/strip-windows.sh`
Expected: one `removed` line per path.

- [ ] **Step 6: Run the test to verify it passes**

Run: `./scripts/verify-vendor.sh; echo "exit=$?"`
Expected: exit=0. Every removed path reports `stripped`, and the widget source count still
reports at least 80 files present.

- [ ] **Step 7: Verify the script is idempotent**

Run: `./scripts/strip-windows.sh && ./scripts/verify-vendor.sh; echo "exit=$?"`
Expected: every line reads `absent`, and the verification still exits 0.

- [ ] **Step 8: Commit**

```bash
git add scripts/strip-windows.sh scripts/verify-vendor.sh
git commit -m "Add the Windows strip script and its safety net"

git add -A src
git commit -m "Strip Windows-only components from the vendored tree"
```

- [ ] **Step 9: Open the pull request**

```bash
git push -u origin chore/strip-windows-only
gh pr create --repo jacksonmafra-umain/machdbg --base main \
  --assignee jacksonmafra-umain --milestone "M0 Upstream alignment" \
  --title "Strip Windows-only components from the vendored tree" \
  --body "Removes TitanEngine, GleeBug, XEDParse, DeviceNameResolver and the Windows exe, launcher and loaddll projects.

src/gui/Src and Zydis both stay. The widget library compiles 81 files out of src/gui/Src, and x64dbg_widgets links zydis_wrapper until the Capstone tokenizer replaces it at milestone 6.

verify-vendor.sh gains the assertion that matters: every file referenced by widgets/CMakeLists.txt must exist on disk. That check is what turns a future over-eager strip into a failing script rather than a broken build.

Closes #<issue>"
```

---

### Task 4: Configure on macOS

The vendored `src/cross/cmake.toml` gates `ElfBug` and `debugger` behind a `linux-x64`
condition, and leaves `minidump`, `remote_table`, `release_notes` and `hex_viewer` ungated.
Those four are what make milestone 1 reachable. This task gets CMake to configure them on macOS;
building them into `.app` bundles is milestone 1.

**Files:**
- Modify: `src/cross/cmake.toml`
- Create: `CMakePresets.json`

**Interfaces:**
- Consumes: `scripts/check-toolchain.sh` from Task 1, the stripped tree from Task 3.
- Produces: presets `macos-arm64` and `macos-universal`; cmkr conditions `macos-arm64`,
  `macos-x64` and `macos`. Every later milestone's targets attach to the `macos` condition.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Configure the vendored build on macOS" \
  --assignee jacksonmafra-umain --milestone "M0 Upstream alignment" \
  --label "type:chore,area:build,arch:arm64,arch:x86_64,prio:high" \
  --body "src/cross/cmake.toml has a linux-x64 condition gating ElfBug and debugger. Add macos-arm64, macos-x64 and macos conditions beside it, so later milestones have something to attach MachBug to.

Add CMakePresets.json with macos-arm64 for host-only development builds and macos-universal targeting arm64;x86_64 for releases.

Done when cmake --preset macos-arm64 configures without error and the four ungated Qt targets appear in the generated build files."
```

- [ ] **Step 2: Write the failing test**

Create `CMakePresets.json`:

```json
{
  "version": 3,
  "cmakeMinimumRequired": { "major": 3, "minor": 19, "patch": 0 },
  "configurePresets": [
    {
      "name": "macos-base",
      "hidden": true,
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_OSX_DEPLOYMENT_TARGET": "13.0"
      }
    },
    {
      "name": "macos-arm64",
      "displayName": "macOS, host architecture only",
      "inherits": "macos-base",
      "cacheVariables": { "CMAKE_OSX_ARCHITECTURES": "arm64" }
    },
    {
      "name": "macos-universal",
      "displayName": "macOS, universal binary",
      "inherits": "macos-base",
      "cacheVariables": { "CMAKE_OSX_ARCHITECTURES": "arm64;x86_64" }
    }
  ],
  "buildPresets": [
    { "name": "macos-arm64", "configurePreset": "macos-arm64" },
    { "name": "macos-universal", "configurePreset": "macos-universal" }
  ]
}
```

- [ ] **Step 3: Run it to make sure it fails**

Run: `./scripts/check-toolchain.sh && cmake --preset macos-arm64; echo "exit=$?"`
Expected: a non-zero exit. The failure comes from the top-level CMake project, which is still
the Windows-oriented upstream one. Record the exact error — the next step is driven by it.

- [ ] **Step 4: Add the macOS conditions**

In `src/cross/cmake.toml`, extend the `[conditions]` table. Leave `linux-x64` untouched:

```toml
[conditions]
linux-x64 = "CMAKE_SYSTEM_NAME STREQUAL \"Linux\" AND CMAKE_SYSTEM_PROCESSOR MATCHES \"^(x86_64|amd64|AMD64)$\""
elfbug-tests = "$<linux-x64> AND ELFBUG_BUILD_TESTS"
macos = "CMAKE_SYSTEM_NAME STREQUAL \"Darwin\""
macos-arm64 = "$<macos> AND CMAKE_SYSTEM_PROCESSOR MATCHES \"^(arm64|aarch64)$\""
macos-x64 = "$<macos> AND CMAKE_SYSTEM_PROCESSOR MATCHES \"^(x86_64|amd64|AMD64)$\""
machbug-tests = "$<macos> AND MACHBUG_BUILD_TESTS"
```

Add the option beside the existing ones:

```toml
[options]
LIBPL_ENABLE_CLI = false
LIBPL_ENABLE_TESTS = false
LIBWOLV_ENABLE_TESTS = false
ELFBUG_BUILD_TESTS = false
MACHBUG_BUILD_TESTS = false
```

Regenerate: `cmkr gen`

- [ ] **Step 5: Configure until it succeeds**

Run: `cmake --preset macos-arm64`
Expected: `-- Configuring done`. Resolve failures as they appear; they will be Windows-only
subdirectories still referenced by the top-level project. Remove those references rather than
guarding them — the tree is a fork and dead references are noise.

- [ ] **Step 6: Verify the Qt targets exist**

Run: `cmake --build build/macos-arm64 --target help | grep -E 'hex_viewer|minidump|remote_table|release_notes'`
Expected: all four target names appear.

- [ ] **Step 7: Commit**

```bash
git add CMakePresets.json src/cross/cmake.toml src/cross/CMakeLists.txt
git commit -m "Add macOS build conditions and presets"
```

- [ ] **Step 8: Open the pull request**

```bash
git push -u origin chore/macos-build-conditions
gh pr create --repo jacksonmafra-umain/machdbg --base main \
  --assignee jacksonmafra-umain --milestone "M0 Upstream alignment" \
  --title "Configure the vendored build on macOS" \
  --body "Adds macos, macos-arm64, macos-x64 and machbug-tests conditions to src/cross/cmake.toml beside the existing linux-x64 ones, plus CMakePresets.json with a host-only development preset and a universal release preset.

cmake --preset macos-arm64 configures, and the four ungated Qt targets — hex_viewer, minidump, remote_table and release_notes — are present in the generated build. Turning them into .app bundles is milestone 1.

Closes #<issue>"
```

---

### Task 5: The engine contract header

Section 5 of the design document specifies a vtable rather than the direct exports `ElfBug`
uses, so that a remote backend can be added later without touching `DbgAdapter`. This task
writes that header and proves it is implementable, in C and in C++, before any engine exists.

**Files:**
- Create: `src/cross/MachBug/MachBug/api/machbug_api.h`
- Create: `src/cross/MachBug/tests/api_contract.cpp`
- Create: `src/cross/MachBug/tests/cmake.toml`
- Modify: `src/cross/cmake.toml`

**Interfaces:**
- Consumes: the macOS conditions from Task 4.
- Produces: `DbgEngine`, `DbgEngineCallbacks`, `DbgStatus`, `DbgArch`, `DbgRegsArm64`,
  `DbgRegsX86_64`, `DbgRegisters`, `DbgRegisterDesc`, `DbgBreakpointKind`, `DbgLaunchSpec`, and
  `MachBugCreate` / `MachBugDestroy`. Milestone 2 implements the vtable; milestone 3 fills the
  register descriptor tables.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Add the MachBug engine contract header" \
  --assignee jacksonmafra-umain --milestone "M0 Upstream alignment" \
  --label "type:feature,area:machbug,prio:high" \
  --body "Section 5 of the design document specifies the engine contract as a vtable rather than the direct exported symbols ElfBug uses, so that a second backend can be added later without changing DbgAdapter.

Write src/cross/MachBug/MachBug/api/machbug_api.h with the vtable, the tagged register union, the register descriptor table type, the architecture-neutral breakpoint kinds and the twelve callbacks. Prove it compiles as C and as C++ and that a stub engine can fill the vtable, with a Catch2 test built on the machbug-tests condition.

The header must not name DR7, a debug register, or any other architecture-specific register, and kern_return_t must not appear in it.

Done when the contract test passes."
```

- [ ] **Step 2: Write the failing test**

Create `src/cross/MachBug/tests/api_contract.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <MachBug/api/machbug_api.h>

#include <cstring>
#include <string>

namespace
{
    // A stub engine proves the vtable is fillable without an implementation existing.
    struct StubEngine
    {
        int continueCalls = 0;
        bool started = false;
    };

    DbgStatus stubStart(void* impl, const DbgLaunchSpec* spec)
    {
        if(spec == nullptr || spec->path == nullptr)
            return DbgStatus_InvalidArgument;
        static_cast<StubEngine*>(impl)->started = true;
        return DbgStatus_Ok;
    }

    DbgStatus stubContinue(void* impl)
    {
        static_cast<StubEngine*>(impl)->continueCalls++;
        return DbgStatus_Ok;
    }
}

TEST_CASE("the vtable can be filled by an engine that does not exist yet")
{
    StubEngine stub;
    DbgEngine engine{};
    engine.impl = &stub;
    engine.Start = stubStart;
    engine.Continue = stubContinue;

    DbgLaunchSpec spec{};
    spec.path = "/usr/bin/true";

    REQUIRE(engine.Start(engine.impl, &spec) == DbgStatus_Ok);
    REQUIRE(stub.started);
    REQUIRE(engine.Continue(engine.impl) == DbgStatus_Ok);
    REQUIRE(stub.continueCalls == 1);
}

TEST_CASE("Start rejects a launch spec with no path")
{
    StubEngine stub;
    DbgLaunchSpec spec{};
    REQUIRE(stubStart(&stub, &spec) == DbgStatus_InvalidArgument);
    REQUIRE_FALSE(stub.started);
}

TEST_CASE("every architecture the port supports has an enum value")
{
    REQUIRE(DbgArch_Unknown == 0);
    REQUIRE(DbgArch_X86_64 != DbgArch_Arm64);
    REQUIRE(DbgArch_Arm64 != DbgArch_Arm64e);
    REQUIRE(DbgArch_I386 != DbgArch_X86_64);
}

TEST_CASE("registers are a tagged union, not a flat x86 layout")
{
    DbgRegisters regs{};
    regs.arch = DbgArch_Arm64;
    regs.arm64.pc = 0x100000000ull;
    REQUIRE(regs.arch == DbgArch_Arm64);
    REQUIRE(regs.arm64.pc == 0x100000000ull);

    regs.arch = DbgArch_X86_64;
    regs.x86_64.rip = 0x140001000ull;
    REQUIRE(regs.x86_64.rip == 0x140001000ull);
}

TEST_CASE("arm64 exposes the full general register file")
{
    DbgRegsArm64 regs{};
    // x0 through x30 inclusive.
    REQUIRE(sizeof(regs.x) / sizeof(regs.x[0]) == 31);
}

TEST_CASE("breakpoint kinds are architecture-neutral")
{
    // The point of the enum is that no caller ever names a debug register.
    REQUIRE(DbgBreakpointKind_Software != DbgBreakpointKind_HwExec);
    REQUIRE(DbgBreakpointKind_HwRead != DbgBreakpointKind_HwWrite);
}

TEST_CASE("a register descriptor table describes registers as data")
{
    static const DbgRegisterDesc sample[] = {
        { 0, "pc", 64, 0, DbgRegisterFlag_ProgramCounter },
        { 1, "sp", 64, 8, DbgRegisterFlag_StackPointer },
    };
    REQUIRE(std::string(sample[0].name) == "pc");
    REQUIRE(sample[0].flags == DbgRegisterFlag_ProgramCounter);
    REQUIRE(sample[1].bits == 64);
}
```

- [ ] **Step 3: Run it to make sure it fails**

Run: `cmake --preset macos-arm64 -DMACHBUG_BUILD_TESTS=ON && cmake --build build/macos-arm64 --target MachBug_tests`
Expected: FAIL, `fatal error: 'MachBug/api/machbug_api.h' file not found`.

- [ ] **Step 4: Write the header**

Create `src/cross/MachBug/MachBug/api/machbug_api.h`:

```c
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MACHBUG_BUILDING
#define MACHBUG_EXPORT __attribute__((visibility("default")))
#else
#define MACHBUG_EXPORT
#endif

typedef enum
{
    DbgStatus_Ok = 0,
    DbgStatus_Failed = 1,
    DbgStatus_InvalidArgument = 2,
    DbgStatus_NotAttached = 3,
    DbgStatus_NotPermitted = 4,   /* task_for_pid denied; see the debuggability report */
    DbgStatus_NotSupported = 5,
    DbgStatus_TargetExited = 6,
} DbgStatus;

typedef enum
{
    DbgArch_Unknown = 0,
    DbgArch_X86_64 = 1,
    DbgArch_I386 = 2,
    DbgArch_Arm64 = 3,
    DbgArch_Arm64e = 4,
} DbgArch;

typedef struct
{
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rbp, rsp, rsi, rdi;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip;
    uint64_t rflags;
    uint16_t cs, ds, es, fs, gs, ss;
    uint64_t fs_base, gs_base;
} DbgRegsX86_64;

typedef struct
{
    uint64_t x[31];    /* x0 through x30; x30 is the link register */
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
} DbgRegsArm64;

typedef struct
{
    DbgArch arch;
    union
    {
        DbgRegsX86_64 x86_64;
        DbgRegsArm64 arm64;
    };
} DbgRegisters;

typedef enum
{
    DbgRegisterFlag_None = 0,
    DbgRegisterFlag_General = 1 << 0,
    DbgRegisterFlag_Flags = 1 << 1,
    DbgRegisterFlag_Vector = 1 << 2,
    DbgRegisterFlag_ProgramCounter = 1 << 3,
    DbgRegisterFlag_StackPointer = 1 << 4,
} DbgRegisterFlag;

/* Views iterate this table instead of reading struct fields, so no view needs to know
   which architecture it is displaying. */
typedef struct
{
    uint32_t id;
    const char* name;
    uint16_t bits;
    uint16_t offset;    /* byte offset into the active arm of DbgRegisters */
    uint32_t flags;     /* a mask of DbgRegisterFlag */
} DbgRegisterDesc;

typedef enum
{
    DbgBreakpointKind_Software = 0,
    DbgBreakpointKind_HwExec = 1,
    DbgBreakpointKind_HwRead = 2,
    DbgBreakpointKind_HwWrite = 3,
} DbgBreakpointKind;

typedef struct
{
    const char* path;
    const char* const* argv;    /* NULL terminated, may be NULL */
    const char* const* envp;    /* NULL terminated, may be NULL */
    const char* workingDirectory;
    pid_t attachPid;            /* 0 to launch rather than attach */
} DbgLaunchSpec;

typedef struct
{
    uint64_t base;
    uint64_t size;
    uint32_t protection;    /* current protection, VM_PROT_* bits */
    uint32_t maxProtection;
    uint32_t userTag;       /* VM_MEMORY_* */
} DbgMemoryRegion;

typedef void (*DbgCbCreateProcess)(pid_t pid, uint64_t entryPoint, void* userdata);
typedef void (*DbgCbExitProcess)(int exitCode, void* userdata);
typedef void (*DbgCbSystemBreakpoint)(void* userdata);
typedef void (*DbgCbBreakpoint)(uint64_t address, void* userdata);
typedef void (*DbgCbStep)(void* userdata);
typedef void (*DbgCbPaused)(void* userdata);
typedef void (*DbgCbError)(const char* error, void* userdata);
typedef void (*DbgCbDebugString)(const char* text, void* userdata);
typedef void (*DbgCbLoadModule)(uint64_t base, const char* path, void* userdata);
typedef void (*DbgCbUnloadModule)(uint64_t base, void* userdata);
typedef void (*DbgCbThreadCreate)(uint64_t threadId, void* userdata);
typedef void (*DbgCbThreadExit)(uint64_t threadId, void* userdata);
typedef void (*DbgCbException)(uint32_t type, uint64_t address, void* userdata);

typedef struct
{
    DbgCbCreateProcess onCreateProcess;
    DbgCbExitProcess onExitProcess;
    DbgCbSystemBreakpoint onSystemBreakpoint;
    DbgCbBreakpoint onBreakpoint;
    DbgCbStep onStep;
    DbgCbPaused onPaused;
    DbgCbError onError;
    DbgCbDebugString onDebugString;
    DbgCbLoadModule onLoadModule;
    DbgCbUnloadModule onUnloadModule;
    DbgCbThreadCreate onThreadCreate;
    DbgCbThreadExit onThreadExit;
    DbgCbException onException;
    void* userdata;
} DbgEngineCallbacks;

/* The adapter holds this, never a backend type. Start blocks and owns the event loop;
   Continue, StepInto, Pause and Stop are callable from another thread. */
typedef struct DbgEngine
{
    void* impl;

    DbgStatus (*Start)(void* impl, const DbgLaunchSpec* spec);
    DbgStatus (*Continue)(void* impl);
    DbgStatus (*StepInto)(void* impl);
    DbgStatus (*Pause)(void* impl);
    DbgStatus (*Stop)(void* impl);

    DbgStatus (*GetRegisters)(void* impl, uint64_t threadId, DbgRegisters* out);
    DbgStatus (*SetRegister)(void* impl, uint64_t threadId, const char* name, uint64_t value);
    const DbgRegisterDesc* (*GetRegisterDescs)(void* impl, DbgArch arch, uint32_t* count);
    pid_t (*GetPid)(void* impl);
    DbgArch (*GetArch)(void* impl);

    DbgStatus (*MemRead)(void* impl, uint64_t addr, void* dest, uint64_t size);
    DbgStatus (*MemWrite)(void* impl, uint64_t addr, const void* src, uint64_t size);
    DbgStatus (*MemFindBaseAddr)(void* impl, uint64_t addr, uint64_t* base, uint64_t* size);
    bool (*MemIsCodePtr)(void* impl, uint64_t addr);
    bool (*MemIsValidPtr)(void* impl, uint64_t addr);
    DbgStatus (*MemEnumRegions)(void* impl, DbgMemoryRegion* out, uint32_t capacity,
            uint32_t* count);

    DbgStatus (*ModBaseFromAddr)(void* impl, uint64_t addr, uint64_t* base);
    DbgStatus (*ModNameFromAddr)(void* impl, uint64_t addr, char* buf, uint64_t bufSize);

    DbgStatus (*SetBreakpoint)(void* impl, DbgBreakpointKind kind, uint64_t addr, uint32_t size);
    DbgStatus (*DeleteBreakpoint)(void* impl, uint64_t addr);
    bool (*IsBreakpointEffective)(void* impl, uint64_t addr);
    uint32_t (*GetHwBreakpointSlots)(void* impl);
} DbgEngine;

MACHBUG_EXPORT DbgEngine* MachBugCreate(const DbgEngineCallbacks* callbacks);
MACHBUG_EXPORT void MachBugDestroy(DbgEngine* engine);

/* Detail for the last DbgStatus returned on this thread. Never a kern_return_t. */
MACHBUG_EXPORT const char* DbgLastErrorString(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 5: Wire up the test target**

Create `src/cross/MachBug/tests/cmake.toml`:

```toml
# Reference: https://build-cpp.github.io/cmkr/cmake-toml
[cmake]
version = "3.19"
cmkr-include = false

[project]
name = "MachBug_tests"
languages = ["C", "CXX"]

[fetch-content.Catch2]
git = "https://github.com/catchorg/Catch2"
tag = "v3.14.0"
shallow = true

[target.MachBug_tests]
type = "executable"
sources = ["api_contract.cpp", "api_contract_c.c"]
compile-features = ["cxx_std_17"]
include-directories = ["../"]
link-libraries = ["Catch2::Catch2WithMain"]
properties.RUNTIME_OUTPUT_DIRECTORY = "${CMAKE_BINARY_DIR}/tests"
cmake-after = """
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(MachBug_tests)
"""
```

Add the subdirectory to `src/cross/cmake.toml`, beside the existing `ElfBug/tests` entry:

```toml
[subdir."MachBug/tests"]
condition = "machbug-tests"
```

Create `src/cross/MachBug/tests/api_contract_c.c`, which exists only to prove the header is
valid C and not accidentally C++:

```c
#include <MachBug/api/machbug_api.h>

/* Compiling this file is the test: the contract must be usable from plain C. */
DbgArch machbug_test_c_arch(void)
{
    DbgRegisters regs;
    regs.arch = DbgArch_Arm64;
    regs.arm64.pc = 0;
    return regs.arch;
}
```

Regenerate: `cmkr gen`

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --preset macos-arm64 -DMACHBUG_BUILD_TESTS=ON && cmake --build build/macos-arm64 --target MachBug_tests && ./build/macos-arm64/tests/MachBug_tests`
Expected: PASS, seven test cases.

- [ ] **Step 7: Verify the header names no architecture-specific debug register**

Run: `grep -inE 'dr[0-7]|debug_state|kern_return' src/cross/MachBug/MachBug/api/machbug_api.h; echo "exit=$?"`
Expected: no output, exit=1. Both are contract requirements from sections 5 and 6 of the design
document.

- [ ] **Step 8: Commit**

```bash
git add src/cross/MachBug src/cross/cmake.toml src/cross/CMakeLists.txt
git commit -m "Add the MachBug engine contract header and its tests"
```

- [ ] **Step 9: Open the pull request**

```bash
git push -u origin feat/machbug-api-header
gh pr create --repo jacksonmafra-umain/machdbg --base main \
  --assignee jacksonmafra-umain --milestone "M0 Upstream alignment" \
  --title "Add the MachBug engine contract header" \
  --body "The engine contract from section 5 of the design document: a DbgEngine vtable rather than ElfBug's direct exports, so a remote backend can be added later without touching DbgAdapter.

Registers are a tagged union with a descriptor table, so views iterate data instead of branching on architecture. Breakpoints are described by kind and size, never by debug register. DbgStatus replaces kern_return_t at the boundary, because a remote backend has none to return.

Tested three ways: a stub engine fills the vtable and is called, a C translation unit compiles the header to prove it is valid C, and a grep asserts the header names no DR register and no kern_return_t.

Closes #<issue>"
```

---

### Task 6: Licences and credits

Section 14 of the design document makes this milestone 0 work rather than packaging work,
because a GPLv3 fork that ships without attribution is a licence violation from its first
commit, not from its first release.

**Files:**
- Create: `CREDITS.md`
- Create: `docs/licenses.md`
- Create: `scripts/check-credits.sh`
- Modify: `README.md`

**Interfaces:**
- Consumes: nothing.
- Produces: `scripts/check-credits.sh`, exit 0 when every required name and licence is present.
  Later milestones add a line to `docs/licenses.md` whenever they add a dependency.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Add the credits and licence files" \
  --assignee jacksonmafra-umain --milestone "M0 Upstream alignment" \
  --label "type:docs,prio:high" \
  --body "Section 14 of the design document requires attribution in place from the start, not at release. A GPLv3 fork without credits is non-compliant from its first commit.

Add CREDITS.md naming the x64dbg authors, the cross-platform groundwork, the upstream dependencies, the reference plugins and the macOS prior art. Add docs/licenses.md listing only what actually ships. Add scripts/check-credits.sh so a missing name fails a check rather than going unnoticed. Point README.md at both.

Done when the check script passes."
```

- [ ] **Step 2: Write the failing test**

Create `scripts/check-credits.sh`:

```bash
#!/usr/bin/env bash
# Fails if a required credit or licence entry is missing.
set -uo pipefail

cd "$(dirname "$0")/.."

failed=0

require_in() {
    local file="$1" needle="$2"
    if [[ ! -f "$file" ]]; then
        printf 'FAIL %s is missing\n' "$file"
        failed=1
        return
    fi
    if grep -qi -- "$needle" "$file"; then
        printf 'ok %s mentions %s\n' "$file" "$needle"
    else
        printf 'FAIL %s does not mention %s\n' "$file" "$needle"
        failed=1
    fi
}

for name in mrexodia Sigma tr4ceflow Dreg Nukem Herz3h torusrxxx 3rdit eldarkg \
            "Pedro Vila" StackContains DrDecode VisualPharm Fugue; do
    require_in CREDITS.md "$name"
done

for component in GPLv3 "Qt 6" Capstone asmjit jansson lz4 yara; do
    require_in docs/licenses.md "$component"
done

require_in README.md CREDITS.md

exit "$failed"
```

- [ ] **Step 3: Run it to make sure it fails**

Run: `chmod +x scripts/check-credits.sh && ./scripts/check-credits.sh; echo "exit=$?"`
Expected: exit=1, `FAIL CREDITS.md is missing` repeated, and `FAIL docs/licenses.md is missing`.

- [ ] **Step 4: Write the credits**

Create `CREDITS.md`:

```markdown
# Credits

machdbg is an independent GPLv3 fork of [x64dbg](https://github.com/x64dbg/x64dbg). Everything
below is credited whether or not its code survives in this tree.

## x64dbg

mrexodia (Duncan Ogilvie), Sigma (initial GUI), tr4ceflow, Dreg, Nukem, Herz3h, torusrxxx, and
the [contributor list](https://github.com/x64dbg/x64dbg/graphs/contributors).

## Cross-platform groundwork

- **@3rdit** — the ElfBug engine and the cross-platform debugger shell this port mirrors.
- **@eldarkg** — the Wine build documentation.

## Upstream dependencies

TitanEngine Community Edition, Zydis, XEDParse, asmjit, Scylla, Jansson, lz4, the bug icon by
VisualPharm, interface icons by Fugue, website by tr4ceflow.

Some of these no longer ship in machdbg. They are credited because the code that grew around
them does.

## Reference plugins

- [mrexodia/StackContains](https://github.com/mrexodia/StackContains) — the Tier 1 plugin
  acceptance test.
- [mrexodia/DrDecode](https://github.com/mrexodia/DrDecode) — the architecture-bound plugin the
  tiering was designed against.

## macOS prior art

[gdbinit](https://github.com/gdbinit/Gdbinit) by Pedro Vilaça (fG!) — a design reference for
register and context display conventions on macOS. Its licence is checked before any code or
layout is reused; the credit stands either way.
```

- [ ] **Step 5: Write the licence list**

Create `docs/licenses.md`:

```markdown
# Licences

machdbg is GPLv3. As a derivative work of x64dbg it cannot be anything else, and its source is
published.

## Plugin exception

Inherited from x64dbg and preserved deliberately: plugins may be closed-source, commercial or
private, unless they copy code from machdbg or x64dbg. Removing this would take away a right
plugin authors have today.

## What ships

| Component | Licence |
|---|---|
| machdbg | GPLv3 |
| Qt 6 | LGPLv3 |
| Capstone | BSD-3-Clause |
| asmjit / asmtk | zlib |
| jansson | MIT |
| lz4 | BSD |
| yara | BSD-3-Clause |
| DWARF parser | to be decided at milestone 7 |

Qt under LGPL in a macOS `.app` needs the usual relinking and attribution care.

## What no longer ships

TitanEngine, GleeBug, XEDParse and Scylla were removed at milestone 0. Zydis is removed at
milestone 6, when the Capstone tokenizer replaces it. All four remain credited in `CREDITS.md`.

## Adding a dependency

Add its row to the table above in the same commit that introduces it. `scripts/check-credits.sh`
is the enforcement.
```

- [ ] **Step 6: Point the README at both**

In `README.md`, replace the line reading
`See [CREDITS](CREDITS.md) once it lands for the full list, including upstream dependencies and`
and the line after it with:

```markdown
See [CREDITS.md](CREDITS.md) for the full list and [docs/licenses.md](docs/licenses.md) for the
licences of everything that ships.
```

- [ ] **Step 7: Run the test to verify it passes**

Run: `./scripts/check-credits.sh; echo "exit=$?"`
Expected: exit=0, every line `ok`.

- [ ] **Step 8: Commit**

```bash
git add CREDITS.md docs/licenses.md scripts/check-credits.sh README.md
git commit -m "Add the credits and licence files"
```

- [ ] **Step 9: Open the pull request**

```bash
git push -u origin docs/credits-and-licenses
gh pr create --repo jacksonmafra-umain/machdbg --base main \
  --assignee jacksonmafra-umain --milestone "M0 Upstream alignment" \
  --title "Add the credits and licence files" \
  --body "CREDITS.md names the x64dbg authors, the cross-platform groundwork by @3rdit and @eldarkg, the upstream dependencies, the two reference plugins and gdbinit as macOS prior art. Components that no longer ship are still credited, because the code that grew around them does.

docs/licenses.md lists only what ships, and carries the plugin exception verbatim so plugin authors keep the right they have upstream.

scripts/check-credits.sh turns a missing name into a failing check rather than something nobody notices.

Closes #<issue>"
```

---

## Milestone 0 exit criteria

All six pull requests merged, and on a clean checkout:

```bash
./scripts/check-toolchain.sh                            # exit 0
./scripts/verify-vendor.sh                               # exit 0
./scripts/check-credits.sh                               # exit 0
cmake --preset macos-arm64 -DMACHBUG_BUILD_TESTS=ON      # configures
cmake --build build/macos-arm64 --target MachBug_tests && ./build/macos-arm64/tests/MachBug_tests
```

What milestone 0 deliberately does not deliver: any debugger behaviour, any `.app` bundle, any
Capstone integration. Those are milestones 1 and beyond, each with its own plan written once the
tree it modifies is on disk.
