# machdbg

A native macOS debugger carrying the x64dbg workflow — the same views, expression parser and
command set — onto Mach-O processes running on Apple Silicon and Intel.

machdbg is an independent GPLv3 fork of [x64dbg](https://github.com/x64dbg/x64dbg). It does not
debug Windows PE binaries, and it is not a replacement for LLDB.

## Status

Milestone 0 is complete: the upstream tree is vendored, stripped and configuring under CMake,
with the engine contract header in place. No debugger behaviour ships yet. See the milestones
and issues for what is planned, [docs/COMPILE-macos.md](docs/COMPILE-macos.md) to build it, and
[docs/upstream.md](docs/upstream.md) for how the vendored tree is kept in sync with x64dbg.

## Licence

GPLv3, inherited from x64dbg. See [LICENSE](LICENSE).

Plugins are covered by x64dbg's plugin exception. Plugins may be closed-source, commercial or private, unless they copy code from machdbg or x64dbg.

## Credits

Built on the work of the x64dbg project: mrexodia (Duncan Ogilvie), Sigma, tr4ceflow, Dreg,
Nukem, Herz3h, torusrxxx and the wider contributor list. The cross-platform groundwork this
port depends on comes from @3rdit (ElfBug, cross debugger) and @eldarkg (Wine build).

See [CREDITS.md](CREDITS.md) for the full list and [docs/licenses.md](docs/licenses.md) for the
licences of everything that ships.
