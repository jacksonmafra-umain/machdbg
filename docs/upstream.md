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
