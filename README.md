# machdbg

A native macOS debugger carrying the x64dbg workflow — the same views, expression parser and
command set — onto Mach-O processes running on Apple Silicon and Intel.

machdbg is an independent GPLv3 fork of [x64dbg](https://github.com/x64dbg/x64dbg). It does not
debug Windows PE binaries, and it is not a replacement for LLDB.

## Status

Pre-alpha. Nothing is implemented yet; the design is being written. See the milestones and
issues for what is planned.

## Licence

GPLv3, inherited from x64dbg. See [LICENSE](LICENSE).

Plugins are covered by x64dbg's plugin exception and may be closed-source, commercial or
private — unless they copy code from machdbg or x64dbg.

## Credits

Built on the work of the x64dbg project: mrexodia (Duncan Ogilvie), Sigma, tr4ceflow, Dreg,
Nukem, Herz3h, torusrxxx and the wider contributor list. The cross-platform groundwork this
port depends on comes from @3rdit (ElfBug, cross debugger) and @eldarkg (Wine build).

See [CREDITS](CREDITS.md) once it lands for the full list, including upstream dependencies and
macOS reverse-engineering prior art.
