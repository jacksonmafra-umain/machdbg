# Upstream provenance

machdbg is a vendor-and-strip fork of [x64dbg](https://github.com/x64dbg/x64dbg), GPLv3.

| Field | Value |
|---|---|
| Repository | `https://github.com/x64dbg/x64dbg.git` |
| Branch | `development` |
| Pinned commit (short, as set in `scripts/vendor-upstream.sh`) | `8794998` |
| Resolved commit (full SHA, as printed by `scripts/vendor-upstream.sh` on this run) | `87949989bdeaa1d7f2960d58ef2b85b5baed7fb9` |
| Commit date | 2026-09-06 |
| Vendored on | 2026-09-07 |

## What was copied

`src/` and `cmake/` in full, plus the root `cmake.toml` and `CMakeLists.txt`, saved as
`upstream-cmake.toml` and `upstream-CMakeLists.txt` for reference rather than used directly.

The upstream `.git` directory is not copied. The vendored files are ordinary machdbg files from
this point on and are edited freely — that is the point of decision D3 in the design document.

## What was removed afterwards

See `scripts/strip-windows.sh` and the assertions in `scripts/verify-vendor.sh`.

## What was changed locally

These paths live inside the vendored `src/` and `cmake/` trees but are machdbg's own work, not
upstream's. `scripts/vendor-upstream.sh` treats them specially: it snapshots them before
overwriting `src/` and `cmake/` with the fresh upstream copy, then restores them afterwards
(`AUTHORED_PATHS` in that script), and `scripts/verify-vendor.sh` asserts both that they exist
and, for the three files upstream also ships, that the local edit is still present.

- `cmake/cmkr.cmake` — the dead submodule bootstrap upstream runs before bootstrapping cmkr
  (`src/dbg/btparser` and a top-level `deps/` directory, neither of which exists in this tree)
  is removed. machdbg is a vendor-and-strip fork (decision D3) that never uses submodules; left
  in place, every configure failed immediately on the missing submodule folder. See commit
  `7e8930d`.
- `src/cross/cmake.toml` — carries the macOS `[conditions]` (`macos`, `macos-arm64`,
  `macos-x64`, `machbug-tests`), the `MACHBUG_BUILD_TESTS` option, and the
  `[subdir."MachBug/tests"]` entry.
- `src/cross/CMakeLists.txt` — the generated file matching the `cmake.toml` edit above
  (regenerate with `cmkr` rather than hand-editing, if `cmkr` is available).
- `src/cross/CMakePresets.json` — the `macos-arm64` / `macos-universal` configure and build
  presets. Upstream does not ship an equivalent file at this path.
- `src/cross/MachBug/MachBug/api/machbug_api.h` and the four files under
  `src/cross/MachBug/tests/` — the MachBug engine contract header and its test suite. Upstream
  has no `MachBug` directory at all.
- `src/cross/widgets/Qt.cmake` — `qt_executable` gains an Apple branch: `qt_add_executable(...
  MACOSX_BUNDLE ...)` in place of `WIN32`, and a `set_target_properties` block that points
  `MACOSX_BUNDLE_INFO_PLIST` at the new `packaging/Info.plist.in` and fills in the
  `MACOSX_BUNDLE_*` variables (bundle name, executable name, `com.machdbg.<target>` identifier,
  version), so the four Qt sample applications build as `.app` bundles instead of bare Mach-O
  executables. Upstream's own `# TODO: support macdeployqt` comment above the Windows
  `windeployqt` block, which this change leaves untouched, is what this and the following two
  tasks address. Unlike the entries above, this file is **not** in `AUTHORED_PATHS` in
  `scripts/vendor-upstream.sh`, so a re-vendor silently overwrites it with upstream's version;
  the Apple branch has to be reapplied by hand after every re-sync. Tasks 3 (bundle icon) and 4
  (`macdeployqt`) extend this same branch in place.

  Task 3 adds, right after the `set_target_properties` block above (not inside it, and without
  touching the `MACOSX_BUNDLE_GUI_IDENTIFIER` line that `scripts/verify-vendor.sh` greps for): a
  `target_sources` call adding `packaging/machdbg.icns` to the target, a
  `set_source_files_properties` call giving it `MACOSX_PACKAGE_LOCATION "Resources"`, and a second
  `set_target_properties` call setting `MACOSX_BUNDLE_ICON_FILE "machdbg.icns"`. The `.icns` path
  is spelled `${CMAKE_SOURCE_DIR}/../../packaging/machdbg.icns` — two `..` segments, the same
  anchor `MACOSX_BUNDLE_INFO_PLIST` above already uses via
  `${CMAKE_SOURCE_DIR}/../../packaging/Info.plist.in`. It is deliberately not spelled with
  `CMAKE_CURRENT_LIST_DIR`, which was tried first and rejected: `qt_executable` is a `function()`,
  and CMake resolves `CMAKE_CURRENT_LIST_DIR` inside a function against the *call site* (today,
  `src/cross/CMakeLists.txt`, i.e. `src/cross` for every current caller of `qt_executable()`), not
  the directory of the file that defines the function (`src/cross/widgets`, where `Qt.cmake`
  itself lives) — verified with a debug `message()` during configure, not assumed. That
  call-site dependency is invisible today because every current caller happens to live in
  `src/cross`; it would only start resolving somewhere else once a future caller invoked
  `qt_executable()` from a different directory, and the bug would surface as a missing icon rather
  than a configure error. `CMAKE_SOURCE_DIR` has no such dependency, so the icon path is anchored
  on it instead, matching `MACOSX_BUNDLE_INFO_PLIST`. `packaging/machdbg.icns` itself is generated
  from `src/bug.png` (256x256) by `packaging/make-icns.sh` and is committed, so an ordinary build
  needs neither `sips` nor `iconutil`.

  Task 4 adds the macOS counterpart of the Windows `windeployqt` block, closing the
  `# TODO: support macdeployqt` comment that sat above it (that line is now deleted, not just
  left addressed). Beside the existing `WIN32`-gated `${QT_PACKAGE}::windeployqt` imported-target
  block, a second block gated on `APPLE` locates `qmake` the same way, queries
  `QT_INSTALL_PREFIX`, and declares `${QT_PACKAGE}::macdeployqt` as an imported executable at
  `<prefix>/bin/macdeployqt` if that path exists. Inside `qt_executable`'s `APPLE` branch, a
  `POST_BUILD` `add_custom_command` runs `${QT_PACKAGE}::macdeployqt "$<TARGET_BUNDLE_DIR:${tgt}>"
  -always-overwrite`, copying the Qt frameworks into `Contents/Frameworks` and rewriting the
  executable's load commands to `@executable_path`-relative paths. Unlike the Windows
  `windeployqt` step, this carries no once-per-output-directory guard: `TARGET_BUNDLE_DIR` is
  always the target's own `<tgt>.app`, a directory no other target writes into, so nothing races
  the way parallel Windows targets sharing one `RUNTIME_OUTPUT_DIRECTORY` would. Confirmed by
  inspecting the build tree — each of the four bundles is its own top-level directory under
  `build/macos-arm64/`, sharing nothing a concurrent `macdeployqt` run could corrupt.

  Task 5 found, by actually launching the bundles Task 4 produced (`scripts/smoke-launch.sh`),
  that the `macdeployqt` step above is not merely noisy but load-bearing-broken: it rewrites
  install names and strips the copied frameworks and plugins after its own ad-hoc codesign pass,
  which invalidates their signatures — `macdeployqt` warns about this itself
  ("codesign verification error ... invalid signature") but does not correct it. On this machine
  that surfaced as every bundle dying to the kernel's code-signing enforcement the instant it
  paged in the first modified-after-signing dylib (confirmed with `log show` — "CODE SIGNING:
  cs_invalid_page ... denying page sending SIGKILL"), before a window ever appeared. The same
  `add_custom_command` gained two more `COMMAND` lines: `codesign --force --deep --sign -` to
  re-sign the whole bundle ad-hoc after `macdeployqt` runs, and a `cmake -E copy` of
  `libqoffscreen.dylib` into `Contents/PlugIns/platforms` beforehand — `macdeployqt` only copies
  the platform plugin(s) a bundle's link graph actually references (`libqcocoa.dylib` here), so
  the offscreen platform smoke-launch and CI select via `QT_QPA_PLATFORM=offscreen` was absent
  from every bundle until this task added it by hand. Locating `libqoffscreen.dylib` needed a
  second `qmake -query`, this one for `QT_INSTALL_PLUGINS`, added as its own unconditional block
  right before `qt_executable`: it cannot live inside the existing `NOT TARGET
  ${QT_PACKAGE}::macdeployqt` guard immediately above, which turned out to never fire on this Qt
  version — Qt 6.11's own CMake package config already exports a `Qt6::macdeployqt` target, so
  that guard (and the `QT_INSTALL_PREFIX` query beside it) has been dead code since Task 4, kept
  only as a fallback for Qt configurations where Qt does not export that target itself.

## Re-syncing

1. Change `UPSTREAM_COMMIT` in `scripts/vendor-upstream.sh` and update the "Pinned commit" row
   above to the same new short SHA. `scripts/verify-vendor.sh` derives the pin from this file, so
   nothing else needs to change for the pin itself.
2. Commit or stash any pending changes: `scripts/vendor-upstream.sh` refuses to run against a
   dirty tree. Run `./scripts/vendor-upstream.sh`. It overwrites the vendored paths — preserving
   and restoring the files listed above under "What was changed locally" — and, at the end of
   the run, prints the line `done. resolved commit: <sha>`. Copy that full SHA into the
   "Resolved commit" row above — the short pin and the full SHA must both be updated, or the
   table is ambiguous again.
3. Run `./scripts/strip-windows.sh` to reapply the removals.
4. Run `./scripts/verify-vendor.sh`.
5. Review the diff. Local changes to vendored files that are *not* listed under "What was
   changed locally" are overwritten by step 2, so the diff is the upstream merge, and it is
   expected to be large. The files listed under "What was changed locally" are preserved across
   the re-vendor and should not appear in the diff at all; if one does, the restore step failed
   and needs investigating before the diff is reviewed.
