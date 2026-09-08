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
