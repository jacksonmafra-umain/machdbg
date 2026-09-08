# Milestone 1 — Widgets on macOS Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the four Qt executables that already build into real macOS `.app` bundles that a
person can double-click, and prove it in CI on every push.

**Architecture:** The widget library and its four sample applications already compile and link on
Apple Silicon — milestone 0 delivered that. What they are not is applications: `qt_executable` in
`src/cross/widgets/Qt.cmake` produces bare Mach-O executables, because its only deployment branch
is the Windows one. This milestone gives that function a macOS branch — `MACOSX_BUNDLE`, an
`Info.plist`, an icon, and `macdeployqt` — then puts the whole thing behind a CI job so the next
regression is caught by a machine rather than by someone's memory.

**Tech Stack:** CMake 3.19+ via cmkr, Qt 6.11 from Homebrew, Ninja, GitHub Actions, `macdeployqt`,
`iconutil` and `sips` from the Xcode command line tools.

**Spec:** `docs/specs/2026-09-07-macos-port-design.md`

**Predecessor:** `docs/plans/2026-09-07-m0-upstream-alignment.md`, complete. Its carry-over items
are GitHub issue #22 and are **not** in this milestone's scope.

## Global Constraints

Every task's requirements implicitly include this section.

- **Upstream pin:** `x64dbg/x64dbg` `development` commit `8794998`. Changes to vendored files are
  allowed but must be recorded in `docs/upstream.md` under the local-modifications section, the
  way the `cmake/cmkr.cmake` edit already is — a future re-vendoring overwrites them.
- **The three verification scripts must stay green.** `scripts/check-toolchain.sh`,
  `scripts/verify-vendor.sh` and `scripts/check-credits.sh` all exit 0 today. Any task that breaks
  one has broken the milestone. Run them before you commit.
- **`verify-vendor.sh` guards the authored surface.** If your task adds an authored file inside
  `src/` or `cmake/`, add it to that script's `required[]` list in the same commit, or a future
  re-sync will silently delete your work. This is not optional bookkeeping; it is the fix that
  unblocked milestone 0.
- **Build output must be pristine.** Warnings are findings. Task 1 exists because the current
  build emits four linker warnings.
- **Qt:** Homebrew Qt 6, located through `QT_ROOT_DIR="$(brew --prefix qt)"`. It is
  single-architecture, so `macos-universal` cannot link and is out of scope here.
- **Working directory:** the CMake presets live at `src/cross/CMakePresets.json`. Configure and
  build from inside `src/cross`. There is no root CMake project yet.
- **Workflow:** one GitHub issue per task, assigned to `jacksonmafra-umain`, labelled, in milestone
  `M1 Widgets on macOS`. One branch per task off `main`. One pull request per task. Nothing pushed
  to `main` directly and nothing merged without the owner asking.
- **Commits:** English, microcommits. **No assistant attribution of any kind** in commit messages
  or pull request bodies.
- **Code style:** DRY, clean-architecture boundaries where they fit, comments short and only where
  the code cannot explain itself.
- **Starting a task:** `git checkout main && git pull --ff-only && git checkout -b <the branch
  named in that task's pull request step>`.
- **`#<issue>` in a pull request body** is the number `gh issue create` printed in that task's
  first step. Substitute it before running the command.

---

### Task 1: Match the deployment target to Qt

Every link today emits four warnings of the form `building for macOS-13.0, but linking with dylib
… which was built for newer version 14.0`. Homebrew's Qt 6.11 is built against macOS 14, and
`CMakePresets.json` asks for 13.0. The build is not pristine, and a build whose normal output
includes warnings trains everyone to stop reading it.

**Files:**
- Modify: `src/cross/CMakePresets.json`
- Modify: `docs/COMPILE-macos.md`

**Interfaces:**
- Consumes: nothing.
- Produces: a warning-free build for every later task to build on, and
  `CMAKE_OSX_DEPLOYMENT_TARGET` at a value the rest of the milestone inherits.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Match the deployment target to the Qt build" \
  --assignee jacksonmafra-umain --milestone "M1 Widgets on macOS" \
  --label "type:bug,area:build,prio:high" \
  --body "Every link emits four warnings: building for macOS-13.0 while linking Qt frameworks built for 14.0. Homebrew Qt 6.11 targets macOS 14; src/cross/CMakePresets.json asks for 13.0.

Raise CMAKE_OSX_DEPLOYMENT_TARGET to match, and record in docs/COMPILE-macos.md that the floor is set by the Qt in use rather than chosen freely — the official universal Qt needed for macos-universal may permit a lower floor later.

Done when a clean configure and build of the four Qt targets emits no linker warnings."
```

- [ ] **Step 2: Reproduce the warnings**

```bash
export QT_ROOT_DIR="$(brew --prefix qt)"
cd src/cross
rm -rf build/macos-arm64
cmake --preset macos-arm64 >/dev/null
cmake --build build/macos-arm64 --target hex_viewer 2>&1 | grep -c 'was built for newer version'
```

Expected: a non-zero count. Record the exact warning text in your report.

- [ ] **Step 3: Read the floor Qt actually requires**

```bash
otool -l "$(brew --prefix qt)/lib/QtCore.framework/QtCore" | grep -A3 LC_BUILD_VERSION | head -6
```

Use the `minos` value you see. Do not assume 14.0 because this plan says so — read it.

- [ ] **Step 4: Raise the deployment target**

In `src/cross/CMakePresets.json`, in the `macos-base` preset's `cacheVariables`, change
`CMAKE_OSX_DEPLOYMENT_TARGET` from `"13.0"` to the value from Step 3.

- [ ] **Step 5: Verify the warnings are gone**

```bash
cd src/cross
rm -rf build/macos-arm64
cmake --preset macos-arm64 >/dev/null
cmake --build build/macos-arm64 --target hex_viewer minidump remote_table release_notes 2>&1 \
  | grep -c 'was built for newer version'
```

Expected: `0`. If any other warning appears, report it rather than ignoring it — this task's
deliverable is a pristine build, not a smaller pile of warnings.

- [ ] **Step 6: Document why the floor is what it is**

In `docs/COMPILE-macos.md`, add two sentences near the Qt requirement: the deployment target is
dictated by the Qt build in use, not chosen; Homebrew Qt sets it at the value you found, and the
official universal Qt required for `macos-universal` may permit a lower one.

- [ ] **Step 7: Confirm the verification scripts still pass**

```bash
cd "$(git rev-parse --show-toplevel)"
export QT_ROOT_DIR="$(brew --prefix qt)"
./scripts/check-toolchain.sh && ./scripts/verify-vendor.sh && ./scripts/check-credits.sh
echo "all=$?"
```

Expected: `all=0`.

- [ ] **Step 8: Commit**

```bash
git add src/cross/CMakePresets.json docs/COMPILE-macos.md
git commit -m "Match the deployment target to the Qt build"
```

- [ ] **Step 9: Open the pull request**

```bash
git push -u origin fix/deployment-target
gh pr create --repo jacksonmafra-umain/machdbg --base main \
  --assignee jacksonmafra-umain --milestone "M1 Widgets on macOS" \
  --title "Match the deployment target to the Qt build" \
  --body "Raises CMAKE_OSX_DEPLOYMENT_TARGET to the floor Homebrew Qt 6.11 was built against, removing four linker warnings from every build of the Qt targets.

docs/COMPILE-macos.md now records that this floor is dictated by the Qt in use rather than chosen, so whoever installs the official universal Qt for macos-universal knows they may be able to lower it.

Closes #<issue>"
```

---

### Task 2: Give `qt_executable` a macOS branch

`src/cross/widgets/Qt.cmake` defines `qt_executable`, which every Qt target in the tree goes
through. Its body reads `qt_add_executable(${tgt} WIN32 ${ARGN})` and then, on Windows only, runs
`windeployqt`. On macOS `WIN32` is ignored, no bundle is requested, and the result is a bare
executable. Line 39 of that file carries upstream's own `# TODO: support macdeployqt`.

This task adds the bundle. Task 4 adds the deployment step.

**Files:**
- Modify: `src/cross/widgets/Qt.cmake`
- Create: `packaging/Info.plist.in`
- Modify: `docs/upstream.md`
- Modify: `scripts/verify-vendor.sh`

**Interfaces:**
- Consumes: Task 1's deployment target.
- Produces: `qt_executable` producing `<name>.app` on Apple platforms, with a populated
  `Info.plist`. Task 3 adds the icon to the same function; Task 4 adds `macdeployqt`; Task 5
  launches the result.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Produce .app bundles from qt_executable on macOS" \
  --assignee jacksonmafra-umain --milestone "M1 Widgets on macOS" \
  --label "type:feature,area:packaging,area:build,prio:high" \
  --body "src/cross/widgets/Qt.cmake's qt_executable passes WIN32 to qt_add_executable and has a Windows-only deployment branch, so macOS gets bare Mach-O executables rather than applications. Upstream's own TODO at line 39 acknowledges the gap.

Add an Apple branch: MACOSX_BUNDLE, a packaging/Info.plist.in template, and the bundle properties CMake fills from it. Leave the Windows branch untouched.

Qt.cmake is a vendored file, so record the modification in docs/upstream.md, and add packaging/Info.plist.in to verify-vendor.sh's required list if it lives under a vendored tree.

Done when hex_viewer builds as hex_viewer.app with a well-formed Info.plist."
```

- [ ] **Step 2: Write the failing test**

Create `scripts/check-bundles.sh`:

```bash
#!/usr/bin/env bash
# Verifies the Qt sample applications are well-formed .app bundles.
set -uo pipefail

cd "$(dirname "$0")/.."

build_dir="src/cross/build/macos-arm64"
failed=0

fail() { printf 'FAIL %s\n' "$1"; failed=1; }
pass() { printf 'ok %s\n' "$1"; }

for app in hex_viewer minidump remote_table release_notes; do
    bundle="$build_dir/$app.app"
    if [[ ! -d "$bundle" ]]; then
        fail "$app.app was not produced"
        continue
    fi
    if [[ ! -x "$bundle/Contents/MacOS/$app" ]]; then
        fail "$app.app has no executable at Contents/MacOS/$app"
        continue
    fi
    plist="$bundle/Contents/Info.plist"
    if [[ ! -f "$plist" ]]; then
        fail "$app.app has no Info.plist"
        continue
    fi
    identifier="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist" 2>/dev/null)"
    if [[ -z "$identifier" ]]; then
        fail "$app.app Info.plist has no CFBundleIdentifier"
    else
        pass "$app.app ($identifier)"
    fi
done

exit "$failed"
```

- [ ] **Step 3: Run it to make sure it fails**

```bash
chmod +x scripts/check-bundles.sh
./scripts/check-bundles.sh; echo "exit=$?"
```

Expected: `exit=1`, with `FAIL hex_viewer.app was not produced` and one such line per application.

- [ ] **Step 4: Write the Info.plist template**

Create `packaging/Info.plist.in`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>${MACOSX_BUNDLE_EXECUTABLE_NAME}</string>
    <key>CFBundleIconFile</key>
    <string>${MACOSX_BUNDLE_ICON_FILE}</string>
    <key>CFBundleIdentifier</key>
    <string>${MACOSX_BUNDLE_GUI_IDENTIFIER}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>${MACOSX_BUNDLE_BUNDLE_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${MACOSX_BUNDLE_SHORT_VERSION_STRING}</string>
    <key>CFBundleVersion</key>
    <string>${MACOSX_BUNDLE_BUNDLE_VERSION}</string>
    <key>LSMinimumSystemVersion</key>
    <string>${CMAKE_OSX_DEPLOYMENT_TARGET}</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
</dict>
</plist>
```

- [ ] **Step 5: Add the Apple branch to `qt_executable`**

In `src/cross/widgets/Qt.cmake`, inside `function(qt_executable tgt)`, replace the executable
creation so Apple gets a bundle. The Windows path must keep behaving exactly as it does now:

```cmake
function(qt_executable tgt)
    if("${QT_PACKAGE}" STREQUAL "Qt6")
        if(APPLE)
            qt_add_executable(${tgt} MACOSX_BUNDLE ${ARGN})
        else()
            qt_add_executable(${tgt} WIN32 ${ARGN})
        endif()
    else()
        add_executable(${tgt} ${ARGN})
    endif()
    target_link_libraries(${tgt} PRIVATE ${QT_LIBRARIES})

    if(APPLE)
        set_target_properties(${tgt} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/../../packaging/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "${tgt}"
            MACOSX_BUNDLE_EXECUTABLE_NAME "${tgt}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.machdbg.${tgt}"
            MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
        )
    endif()
```

Leave the rest of the function — the `windeployqt` block — untouched.

The `MACOSX_BUNDLE_INFO_PLIST` path above assumes `Qt.cmake` is processed with `CMAKE_SOURCE_DIR`
at `src/cross`. **Verify that assumption rather than trusting it**: print the value with
`message(STATUS "plist: ${CMAKE_SOURCE_DIR}/../../packaging/Info.plist.in")` during a configure
and check the path resolves to the real file. If it does not, use a path anchored on
`CMAKE_CURRENT_LIST_DIR` instead, which is always the directory of `Qt.cmake` itself.

- [ ] **Step 6: Rebuild and run the test**

```bash
export QT_ROOT_DIR="$(brew --prefix qt)"
cd src/cross && rm -rf build/macos-arm64 && cmake --preset macos-arm64 >/dev/null \
  && cmake --build build/macos-arm64 --target hex_viewer minidump remote_table release_notes 2>&1 | tail -3
cd "$(git rev-parse --show-toplevel)" && ./scripts/check-bundles.sh; echo "exit=$?"
```

Expected: `exit=0`, four `ok` lines, each naming `com.machdbg.<app>`.

- [ ] **Step 7: Record the vendored-file modification**

`src/cross/widgets/Qt.cmake` is vendored. Add it to the local-modifications section of
`docs/upstream.md`, next to the `cmake/cmkr.cmake` entry, saying what changed, why, and that a
re-vendoring overwrites it.

- [ ] **Step 8: Guard the new authored file**

`packaging/Info.plist.in` is authored but lives outside `src/` and `cmake/`, so the re-sync cannot
delete it and `verify-vendor.sh` need not guard it. Confirm that by reading
`scripts/vendor-upstream.sh`'s path list, and say in your report which conclusion you reached. If
the script does touch `packaging/`, add the file to `required[]`.

- [ ] **Step 9: Confirm the verification scripts still pass**

```bash
export QT_ROOT_DIR="$(brew --prefix qt)"
./scripts/check-toolchain.sh && ./scripts/verify-vendor.sh && ./scripts/check-credits.sh
echo "all=$?"
```

Expected: `all=0`.

- [ ] **Step 10: Commit**

```bash
git add src/cross/widgets/Qt.cmake packaging/Info.plist.in scripts/check-bundles.sh docs/upstream.md
git commit -m "Produce .app bundles from qt_executable on macOS"
```

- [ ] **Step 11: Open the pull request**

```bash
git push -u origin feat/macos-app-bundles
gh pr create --repo jacksonmafra-umain/machdbg --base main \
  --assignee jacksonmafra-umain --milestone "M1 Widgets on macOS" \
  --title "Produce .app bundles from qt_executable on macOS" \
  --body "qt_executable now takes an Apple branch: MACOSX_BUNDLE, a packaging/Info.plist.in template, and the bundle properties CMake fills from it. The Windows branch is untouched.

scripts/check-bundles.sh is the gate — it asserts each of the four sample applications is a directory with an executable at Contents/MacOS and a CFBundleIdentifier in its Info.plist, rather than merely checking that a file exists.

Qt.cmake is vendored, so the modification is recorded in docs/upstream.md for whoever re-syncs.

Closes #<issue>"
```

---

### Task 3: Give the bundles an icon

A bundle without an icon shows the generic application placeholder in Finder and the Dock. The
repository already carries `src/bug.png`, upstream's bug logo, which is the right mark for a
debugger — but macOS wants `.icns`, and nothing generates one.

**Files:**
- Create: `packaging/make-icns.sh`
- Create: `packaging/machdbg.icns` (generated, committed)
- Modify: `src/cross/widgets/Qt.cmake`
- Modify: `scripts/check-bundles.sh`

**Interfaces:**
- Consumes: `qt_executable`'s Apple branch from Task 2.
- Produces: `MACOSX_BUNDLE_ICON_FILE` populated and the `.icns` copied into
  `Contents/Resources` of every bundle.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Give the macOS bundles an application icon" \
  --assignee jacksonmafra-umain --milestone "M1 Widgets on macOS" \
  --label "type:feature,area:packaging,prio:medium" \
  --body "The .app bundles show the generic placeholder icon. src/bug.png is upstream's logo and the right mark; macOS needs it as .icns.

Add packaging/make-icns.sh building the iconset with sips and iconutil, commit the generated packaging/machdbg.icns so a build does not depend on the tools, wire MACOSX_BUNDLE_ICON_FILE and the RESOURCE placement in Qt.cmake, and extend scripts/check-bundles.sh to assert the icon is inside each bundle.

Credit is already in order: the bug icon is attributed to VisualPharm in CREDITS.md.

Done when each bundle carries the icon and check-bundles.sh verifies it."
```

- [ ] **Step 2: Write the failing test**

In `scripts/check-bundles.sh`, inside the per-application loop, after the `CFBundleIdentifier`
check, add:

```bash
    icon="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "$plist" 2>/dev/null)"
    if [[ -z "$icon" ]]; then
        fail "$app.app Info.plist has no CFBundleIconFile"
    elif [[ ! -f "$bundle/Contents/Resources/$icon" ]]; then
        fail "$app.app declares icon $icon but Contents/Resources/$icon is missing"
    else
        pass "$app.app icon $icon"
    fi
```

- [ ] **Step 3: Run it to make sure it fails**

```bash
./scripts/check-bundles.sh; echo "exit=$?"
```

Expected: `exit=1`, with `FAIL hex_viewer.app Info.plist has no CFBundleIconFile` and one per
application. The identifier lines from Task 2 should still read `ok` — if they do not, stop: you
have broken Task 2's work rather than extended it.

- [ ] **Step 4: Write the icon generator**

Create `packaging/make-icns.sh`:

```bash
#!/usr/bin/env bash
# Builds packaging/machdbg.icns from src/bug.png. Run when the source image changes.
# The result is committed, so building machdbg does not require sips or iconutil.
set -euo pipefail

cd "$(dirname "$0")/.."

source_png="src/bug.png"
iconset="$(mktemp -d)/machdbg.iconset"
mkdir -p "$iconset"

# src/bug.png is 256x256, so 256 is the largest honest size and 128@2x is the
# largest honest retina variant. Asking sips for 512 or 1024 would upscale.
for size in 16 32 128; do
    sips -z "$size" "$size" "$source_png" --out "$iconset/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips -z "$double" "$double" "$source_png" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
sips -z 256 256 "$source_png" --out "$iconset/icon_256x256.png" >/dev/null

iconutil --convert icns "$iconset" --output packaging/machdbg.icns
rm -rf "$(dirname "$iconset")"

printf 'wrote packaging/machdbg.icns (%s bytes)\n' "$(stat -f%z packaging/machdbg.icns)"
```

**On the sizes:** `src/bug.png` is 256x256 — verified, not assumed. macOS iconsets go up to
1024x1024, but generating those from a 256px source produces a blurred icon that looks worse than
a smaller sharp one. If you want the larger sizes, the right fix is a larger source image, which
is out of this milestone's scope. Report the sizes `iconutil` accepted.

- [ ] **Step 5: Generate the icon**

```bash
chmod +x packaging/make-icns.sh
./packaging/make-icns.sh
file packaging/machdbg.icns
```

Expected: a `Mac OS X icon` file. If `sips` rejects `src/bug.png`, report what it says rather than
substituting a different source image.

- [ ] **Step 6: Wire the icon into the bundles**

In `src/cross/widgets/Qt.cmake`, in the Apple branch added by Task 2, add the icon file property
and place the file inside the bundle:

```cmake
        set(_icns "${CMAKE_CURRENT_LIST_DIR}/../../../packaging/machdbg.icns")
        target_sources(${tgt} PRIVATE "${_icns}")
        set_source_files_properties("${_icns}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
        )
        set_target_properties(${tgt} PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "machdbg.icns"
        )
```

Add it alongside the existing `set_target_properties` call rather than replacing it. Verify the
`_icns` path resolves — `CMAKE_CURRENT_LIST_DIR` is the directory holding `Qt.cmake`, so count the
`..` segments against the real tree instead of trusting this plan's count.

- [ ] **Step 7: Rebuild and run the test**

```bash
export QT_ROOT_DIR="$(brew --prefix qt)"
cd src/cross && rm -rf build/macos-arm64 && cmake --preset macos-arm64 >/dev/null \
  && cmake --build build/macos-arm64 --target hex_viewer minidump remote_table release_notes >/dev/null 2>&1
cd "$(git rev-parse --show-toplevel)" && ./scripts/check-bundles.sh; echo "exit=$?"
```

Expected: `exit=0`, eight `ok` lines — an identifier and an icon line per application.

- [ ] **Step 8: Commit**

```bash
git add packaging/make-icns.sh packaging/machdbg.icns src/cross/widgets/Qt.cmake scripts/check-bundles.sh
git commit -m "Give the macOS bundles an application icon"
```

- [ ] **Step 9: Open the pull request**

```bash
git push -u origin feat/bundle-icon
gh pr create --repo jacksonmafra-umain/machdbg --base main \
  --assignee jacksonmafra-umain --milestone "M1 Widgets on macOS" \
  --title "Give the macOS bundles an application icon" \
  --body "packaging/make-icns.sh builds machdbg.icns from src/bug.png with sips and iconutil. The generated .icns is committed, so an ordinary build needs neither tool.

Qt.cmake places the icon in Contents/Resources and declares it in the Info.plist; check-bundles.sh now asserts both the declaration and the file, so an icon that is declared but missing fails rather than silently showing the placeholder.

The bug icon is VisualPharm's and is already credited in CREDITS.md.

Closes #<issue>"
```

---

### Task 4: Make the bundles self-contained with `macdeployqt`

The bundles produced so far link Qt from `/opt/homebrew`. They run on this machine and nowhere
else. `macdeployqt` copies the frameworks inside and rewrites the load paths — it is the macOS
counterpart of the `windeployqt` step already in `Qt.cmake`, and the subject of upstream's TODO.

**Files:**
- Modify: `src/cross/widgets/Qt.cmake`
- Modify: `scripts/check-bundles.sh`

**Interfaces:**
- Consumes: the bundles from Tasks 2 and 3.
- Produces: bundles whose Qt frameworks resolve inside `Contents/Frameworks`. Task 5 launches
  them; Task 6 builds them in CI.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Deploy Qt into the bundles with macdeployqt" \
  --assignee jacksonmafra-umain --milestone "M1 Widgets on macOS" \
  --label "type:feature,area:packaging,prio:high" \
  --body "The .app bundles link Qt from /opt/homebrew, so they run only on a machine with that exact Homebrew Qt. macdeployqt copies the frameworks into Contents/Frameworks and rewrites the load commands.

Add the macOS counterpart of the windeployqt block already in src/cross/widgets/Qt.cmake, including the same once-per-output-directory guard so parallel targets do not race. Line 39's TODO is this task.

Extend scripts/check-bundles.sh to assert with otool that no Qt framework resolves outside the bundle — the check must fail if a bundle still points at /opt/homebrew.

Done when otool shows every Qt load path inside the bundle."
```

- [ ] **Step 2: Write the failing test**

In `scripts/check-bundles.sh`, inside the per-application loop, after the icon check, add:

```bash
    external="$(otool -L "$bundle/Contents/MacOS/$app" \
        | awk '/Qt[A-Za-z]*\.framework/ && $1 !~ /^@(executable_path|rpath|loader_path)/ {print $1}')"
    if [[ -n "$external" ]]; then
        fail "$app.app resolves Qt outside the bundle: $(echo "$external" | tr '\n' ' ')"
    else
        pass "$app.app Qt resolves inside the bundle"
    fi
```

- [ ] **Step 3: Run it to make sure it fails**

```bash
./scripts/check-bundles.sh; echo "exit=$?"
```

Expected: `exit=1`, with each application reporting Qt frameworks resolving from
`/opt/homebrew/...`. The identifier and icon lines must still read `ok`.

- [ ] **Step 4: Add the `macdeployqt` imported target**

In `src/cross/widgets/Qt.cmake`, beside the existing `windeployqt` discovery block, add an Apple
equivalent. Follow the structure of the Windows block — locate `qmake`, query
`QT_INSTALL_PREFIX`, build the tool path, and only declare the imported target if the binary
exists:

```cmake
if(${QT_PACKAGE}_FOUND AND APPLE AND TARGET ${QT_PACKAGE}::qmake AND NOT TARGET ${QT_PACKAGE}::macdeployqt)
    get_target_property(_qt_qmake_location ${QT_PACKAGE}::qmake IMPORTED_LOCATION)

    execute_process(
        COMMAND "${_qt_qmake_location}" -query QT_INSTALL_PREFIX
        RESULT_VARIABLE return_code
        OUTPUT_VARIABLE qt_install_prefix
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    set(imported_location "${qt_install_prefix}/bin/macdeployqt")

    if(EXISTS ${imported_location})
        add_executable(${QT_PACKAGE}::macdeployqt IMPORTED)
        set_target_properties(${QT_PACKAGE}::macdeployqt PROPERTIES
            IMPORTED_LOCATION ${imported_location}
        )
    endif()
endif()
```

Homebrew installs `macdeployqt` under the `qt` prefix; if `EXISTS` is false, find where it
actually is with `ls "$(brew --prefix qt)/bin" | grep deploy` and adjust, reporting what you found.

- [ ] **Step 5: Run it after each bundle is built**

In `qt_executable`'s Apple branch, add a post-build step mirroring the Windows one, including its
once-per-directory guard so parallel targets do not deploy into the same place concurrently:

```cmake
    if(APPLE AND TARGET ${QT_PACKAGE}::macdeployqt)
        add_custom_command(TARGET ${tgt} POST_BUILD
            COMMAND ${QT_PACKAGE}::macdeployqt "$<TARGET_BUNDLE_DIR:${tgt}>" -always-overwrite
            COMMENT "Running macdeployqt on ${tgt}..."
        )
    endif()
```

Unlike Windows, each bundle is its own directory, so a shared-directory guard is unnecessary
here — say so in your report rather than copying the Windows guard without thinking.

- [ ] **Step 6: Rebuild and run the test**

```bash
export QT_ROOT_DIR="$(brew --prefix qt)"
cd src/cross && rm -rf build/macos-arm64 && cmake --preset macos-arm64 >/dev/null \
  && cmake --build build/macos-arm64 --target hex_viewer minidump remote_table release_notes 2>&1 | tail -3
cd "$(git rev-parse --show-toplevel)" && ./scripts/check-bundles.sh; echo "exit=$?"
```

Expected: `exit=0`, twelve `ok` lines. Note the build time — `macdeployqt` copies frameworks and
is slow; record it, because Task 6 runs it in CI.

- [ ] **Step 7: Confirm a bundle really is portable**

```bash
du -sh src/cross/build/macos-arm64/hex_viewer.app
otool -L src/cross/build/macos-arm64/hex_viewer.app/Contents/MacOS/hex_viewer | head -8
```

Expected: the bundle is tens of megabytes, and the Qt lines start with `@rpath` or
`@executable_path`. Record both in your report.

- [ ] **Step 8: Update the upstream note**

`docs/upstream.md` already records the `Qt.cmake` modification from Task 2. Extend that entry to
mention the `macdeployqt` addition, so the re-syncer knows the full extent of the local change.

- [ ] **Step 9: Commit**

```bash
git add src/cross/widgets/Qt.cmake scripts/check-bundles.sh docs/upstream.md
git commit -m "Deploy Qt into the bundles with macdeployqt"
```

- [ ] **Step 10: Open the pull request**

```bash
git push -u origin feat/macdeployqt
gh pr create --repo jacksonmafra-umain/machdbg --base main \
  --assignee jacksonmafra-umain --milestone "M1 Widgets on macOS" \
  --title "Deploy Qt into the bundles with macdeployqt" \
  --body "Closes the TODO upstream left at Qt.cmake line 39. The bundles now carry their Qt frameworks and rewrite their load commands, so they run on a machine without Homebrew Qt.

The gate is an otool assertion rather than a claim: check-bundles.sh fails if any Qt framework still resolves outside the bundle, which is the failure this task exists to prevent and the one that is invisible on the machine that built it.

Closes #<issue>"
```

---

### Task 5: Prove the applications launch, with screenshots

Milestone 1's stated proof in the design document is "screenshots, CI job". A bundle that is
well-formed and self-contained can still fail to start — a missing plugin, a bad
`QT_PLUGIN_PATH`, a crash on the first window. This task launches two applications and captures
what they look like.

**Files:**
- Create: `scripts/smoke-launch.sh`
- Create: `docs/screenshots/` with the captured images
- Modify: `README.md`

**Interfaces:**
- Consumes: the deployed bundles from Task 4.
- Produces: `scripts/smoke-launch.sh`, which Task 6 runs in CI.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Smoke-launch the bundles and capture screenshots" \
  --assignee jacksonmafra-umain --milestone "M1 Widgets on macOS" \
  --label "type:feature,area:widgets,prio:high" \
  --body "The design document's proof for milestone 1 is screenshots and a CI job. A bundle can be well-formed and self-contained and still fail to start — a missing Qt plugin, a bad plugin path, a crash on first window.

Add scripts/smoke-launch.sh, which launches each bundle, waits for it to be alive, confirms it did not exit or crash, and terminates it. Capture screenshots of hex_viewer and minidump into docs/screenshots/ and reference them from README.md.

The script must be usable from CI, where no window server may be available — decide how it behaves there and make that behaviour explicit rather than accidental.

Done when the script exits 0 locally and the screenshots are in the README."
```

- [ ] **Step 2: Write the failing test**

Create `scripts/smoke-launch.sh`:

```bash
#!/usr/bin/env bash
# Launches each bundle, confirms it stays alive, then terminates it.
# Set MACHDBG_SMOKE_OFFSCREEN=1 where no window server is available.
set -uo pipefail

cd "$(dirname "$0")/.."

build_dir="src/cross/build/macos-arm64"
failed=0

fail() { printf 'FAIL %s\n' "$1"; failed=1; }
pass() { printf 'ok %s\n' "$1"; }

if [[ "${MACHDBG_SMOKE_OFFSCREEN:-0}" == "1" ]]; then
    export QT_QPA_PLATFORM=offscreen
fi

for app in hex_viewer minidump; do
    binary="$build_dir/$app.app/Contents/MacOS/$app"
    if [[ ! -x "$binary" ]]; then
        fail "$app has no executable to launch"
        continue
    fi

    log="$(mktemp)"
    "$binary" >"$log" 2>&1 &
    pid=$!

    # Give the application time to reach its first window or die trying.
    for _ in $(seq 1 20); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.25
    done

    if kill -0 "$pid" 2>/dev/null; then
        pass "$app stayed alive"
        kill "$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
    else
        wait "$pid" 2>/dev/null
        status=$?
        fail "$app exited early with status $status: $(head -3 "$log" | tr '\n' ' ')"
    fi
    rm -f "$log"
done

exit "$failed"
```

- [ ] **Step 3: Run it**

```bash
chmod +x scripts/smoke-launch.sh
./scripts/smoke-launch.sh; echo "exit=$?"
```

Two outcomes are both informative, and you must report which you got. If it exits 0, the
applications launch and you proceed. If an application exits early, **that is the bug this task
exists to find** — diagnose it from the captured output before going further, and report the
diagnosis. Do not weaken the check to make it pass.

- [ ] **Step 4: Verify the offscreen path**

```bash
MACHDBG_SMOKE_OFFSCREEN=1 ./scripts/smoke-launch.sh; echo "exit=$?"
```

Expected: `exit=0`. This is the mode Task 6 uses in CI, where a window server may not exist. If
offscreen behaves differently from the windowed run, say how — CI is worth little if it exercises
a different path from the one users take.

- [ ] **Step 5: Capture the screenshots**

Launch each application, let its window appear, and capture it:

```bash
mkdir -p docs/screenshots
open src/cross/build/macos-arm64/hex_viewer.app
sleep 3
screencapture -o -w docs/screenshots/hex_viewer.png
```

`screencapture -w` waits for you to click the window to capture. If interactive capture is not
possible in your environment, use `screencapture -o -l "$(GetWindowID hex_viewer 2>/dev/null)"`
or capture the full screen with `screencapture -o docs/screenshots/hex_viewer.png` and say which
you used. Repeat for `minidump`. Quit both applications afterwards.

Keep the images reasonable — if either exceeds about 1 MB, downscale with
`sips -Z 1600 docs/screenshots/<name>.png`.

- [ ] **Step 6: Put the screenshots in the README**

In `README.md`, under the Status section, add a short subsection showing both images with
Markdown image syntax and one sentence saying these are the cross-platform widget samples running
natively on macOS. Update the Status text if it still implies nothing runs.

- [ ] **Step 7: Confirm the verification scripts still pass**

```bash
export QT_ROOT_DIR="$(brew --prefix qt)"
./scripts/check-toolchain.sh && ./scripts/verify-vendor.sh && ./scripts/check-credits.sh \
  && ./scripts/check-bundles.sh
echo "all=$?"
```

Expected: `all=0`.

- [ ] **Step 8: Commit**

```bash
git add scripts/smoke-launch.sh docs/screenshots README.md
git commit -m "Smoke-launch the bundles and capture screenshots"
```

- [ ] **Step 9: Open the pull request**

```bash
git push -u origin feat/smoke-launch
gh pr create --repo jacksonmafra-umain/machdbg --base main \
  --assignee jacksonmafra-umain --milestone "M1 Widgets on macOS" \
  --title "Smoke-launch the bundles and capture screenshots" \
  --body "scripts/smoke-launch.sh launches hex_viewer and minidump, confirms each stays alive rather than merely starting, and reports the captured output when one exits early. MACHDBG_SMOKE_OFFSCREEN=1 selects the Qt offscreen platform for CI, where no window server is guaranteed.

docs/screenshots/ carries the two captures, and the README shows them — the design document names screenshots as half of milestone 1's proof.

Closes #<issue>"
```

---

### Task 6: Continuous integration

Everything above is verified by whoever happens to run the scripts. This task makes a machine do
it on every push, and answers an open question from the design document while it is there:
research question 3 asks whether GitHub still offers an Intel macOS runner, because the x86-64
half of decision D4 has no other proof.

**Files:**
- Create: `.github/workflows/macos.yml`
- Modify: `docs/specs/2026-09-07-macos-port-design.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: every script the previous tasks produced.
- Produces: a CI job, and a factual answer to research question 3.

- [ ] **Step 1: Open the issue**

```bash
gh issue create --repo jacksonmafra-umain/machdbg \
  --title "Add the macOS CI workflow" \
  --assignee jacksonmafra-umain --milestone "M1 Widgets on macOS" \
  --label "type:chore,area:build,arch:arm64,arch:x86_64,prio:high" \
  --body "Nothing runs the verification scripts except a person who remembers to. Add .github/workflows/macos.yml running on every push and pull request: install the toolchain, configure, build the four Qt targets, run MachBug_tests, and run check-toolchain, verify-vendor, check-credits, check-bundles and smoke-launch.

Add a second job on the Intel runner image, allowed to fail. That job is how research question 3 in the design document gets answered — whether an Intel macOS runner is still available is the only proof the x86-64 half of decision D4 has. Record the answer in the spec whichever way it comes out.

Done when the arm64 job passes on a pull request and the spec records what the Intel job did."
```

- [ ] **Step 2: Write the workflow**

Create `.github/workflows/macos.yml`:

```yaml
name: macOS

on:
  push:
    branches: [main]
  pull_request:

jobs:
  arm64:
    name: Apple Silicon
    runs-on: macos-15
    steps:
      - uses: actions/checkout@v4

      - name: Install the toolchain
        run: |
          brew install ninja qt capstone
          curl -sSfL -o cmkr.zip \
            "$(gh release view --repo build-cpp/cmkr --json assets \
               --jq '.assets[] | select(.name | test("macos")) | .url')"
          unzip -o cmkr.zip -d /usr/local/bin
          chmod +x /usr/local/bin/cmkr
        env:
          GH_TOKEN: ${{ github.token }}

      - name: Verify the toolchain
        run: |
          echo "QT_ROOT_DIR=$(brew --prefix qt)" >> "$GITHUB_ENV"
          QT_ROOT_DIR="$(brew --prefix qt)" ./scripts/check-toolchain.sh

      - name: Verify the vendored tree and the credits
        run: |
          ./scripts/verify-vendor.sh
          ./scripts/check-credits.sh

      - name: Configure
        working-directory: src/cross
        run: cmake --preset macos-arm64 -DMACHBUG_BUILD_TESTS=ON

      - name: Build the Qt samples
        working-directory: src/cross
        run: cmake --build build/macos-arm64 --target hex_viewer minidump remote_table release_notes

      - name: Build and run the contract tests
        working-directory: src/cross
        run: |
          cmake --build build/macos-arm64 --target MachBug_tests
          ./build/macos-arm64/tests/MachBug_tests

      - name: Verify the bundles
        run: ./scripts/check-bundles.sh

      - name: Smoke-launch
        run: MACHDBG_SMOKE_OFFSCREEN=1 ./scripts/smoke-launch.sh

  intel:
    name: Intel (answers research question 3)
    runs-on: macos-15-intel
    continue-on-error: true
    steps:
      - uses: actions/checkout@v4
      - name: Report the runner
        run: |
          uname -m
          sw_vers
```

The Intel job deliberately does nothing but report. Its purpose is to confirm the runner is real
and record what it is; building on it is milestone 3's problem, not this one's.

**Research question 3 is already partly answered, and the runner labels here reflect it.** The
image the spec worried about, `macos-13`, is gone. Intel has not gone with it: GitHub's runner
image list offers `macos-15-intel` and `macos-26-intel` as x64 labels. `macos-14` is deprecated,
which is why the arm64 job runs on `macos-15`. Verify all of this against the runner-image README
as your first step rather than trusting these labels — they were read on 2026-09-08 and this is
exactly the kind of fact that moves.

- [ ] **Step 3: Push the branch and watch the run**

```bash
git add .github/workflows/macos.yml
git commit -m "Add the macOS CI workflow"
git push -u origin ci/macos-workflow
gh pr create --repo jacksonmafra-umain/machdbg --base main \
  --assignee jacksonmafra-umain --milestone "M1 Widgets on macOS" \
  --title "Add the macOS CI workflow" \
  --body "Runs on every push to main and every pull request: toolchain check, vendored-tree and credits verification, configure, build of the four Qt samples, the MachBug contract tests, the bundle assertions, and an offscreen smoke launch.

A second job on the Intel image reports its architecture and macOS version and is allowed to fail. That job exists to answer research question 3 in the design document — whether an Intel runner is still available is the only proof the x86-64 half of decision D4 has.

Closes #<issue>"
gh run watch "$(gh run list --branch ci/macos-workflow --limit 1 --json databaseId --jq '.[0].databaseId')"
```

- [ ] **Step 4: Fix what the run reveals**

The first CI run of anything fails. Common causes here: the cmkr release asset name differs from
the pattern; Homebrew's `qt` formula name; `macdeployqt` slow enough to hit a step timeout; the
offscreen platform plugin missing from a Homebrew Qt.

Fix them one at a time, pushing each fix as its own commit, until the arm64 job is green. Do not
delete a step to make the job pass — if a step cannot work in CI, report that rather than
removing the coverage.

- [ ] **Step 5: Record the Intel answer in the spec**

Read what the Intel job did — did the image exist, what did `uname -m` print, what macOS version.
In `docs/specs/2026-09-07-macos-port-design.md`, rewrite research question 3 in section 13 as an
answered item, stating the date, the runner label, and what it reported. Update section 11's
"Intel coverage problem" paragraph to match the fact rather than the hope.

If the image is gone, say so plainly and state that the `unverified` label is now the fallback for
x86-64 work, exactly as section 11 anticipated.

- [ ] **Step 6: Add the badge**

In `README.md`, add the workflow status badge under the title:

```markdown
[![macOS](https://github.com/jacksonmafra-umain/machdbg/actions/workflows/macos.yml/badge.svg)](https://github.com/jacksonmafra-umain/machdbg/actions/workflows/macos.yml)
```

- [ ] **Step 7: Commit and push the follow-ups**

```bash
git add docs/specs/2026-09-07-macos-port-design.md README.md
git commit -m "Record the Intel runner answer and add the CI badge"
git push
```

---

## Milestone 1 exit criteria

All six pull requests merged, and on a clean checkout:

```bash
export QT_ROOT_DIR="$(brew --prefix qt)"
./scripts/check-toolchain.sh                       # exit 0
./scripts/verify-vendor.sh                         # exit 0
./scripts/check-credits.sh                         # exit 0
cd src/cross && cmake --preset macos-arm64 -DMACHBUG_BUILD_TESTS=ON
cmake --build build/macos-arm64 --target hex_viewer minidump remote_table release_notes
cmake --build build/macos-arm64 --target MachBug_tests && ./build/macos-arm64/tests/MachBug_tests
cd - && ./scripts/check-bundles.sh                 # exit 0
./scripts/smoke-launch.sh                          # exit 0
open src/cross/build/macos-arm64/hex_viewer.app    # a window appears
```

Plus: the arm64 CI job green on the most recent commit to `main`, screenshots of `hex_viewer` and
`minidump` in the README, and research question 3 answered in the spec rather than open.

What milestone 1 deliberately does not deliver: any debugger behaviour, any Mach API call, any
signing or notarization, and any universal binary — Homebrew Qt is single-architecture and
`macos-universal` remains unbuildable until the official Qt is installed. Those are milestones 2,
9 and 10.
