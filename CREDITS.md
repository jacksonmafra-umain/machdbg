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

TitanEngine Community Edition, Zydis, GleeBug, XEDParse, asmjit, Scylla, Jansson, lz4, the bug icon by
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
