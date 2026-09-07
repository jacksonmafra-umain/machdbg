# Licences

machdbg is GPLv3. As a derivative work of x64dbg it cannot be anything else, and its source is
published.

## Plugin exception

Inherited from x64dbg and preserved deliberately. Plugins may be closed-source, commercial or private, unless they copy code from machdbg or x64dbg. Removing this would take away a right plugin authors have today.

## Dependencies and their status

This is not a "what ships" list: several of these are not in the tree at all yet, and a couple
survive only as Windows-only artifacts that nothing on macOS currently links. Each row says so.

| Component | Licence | Status |
|---|---|---|
| machdbg | GPLv3 | Ships — this repository |
| Qt 6 | LGPLv3 | Ships — runtime dependency (Homebrew for development, the official installer for `macos-universal`) |
| Capstone | BSD-3-Clause | Build dependency only (`brew install capstone`); not vendored into this tree |
| asmjit / asmtk | zlib | Not present. Planned for the AArch64 assembler, an open research question for milestone 6 |
| jansson | MIT | Present only as Windows `.lib` blobs and headers under `src/dbg/jansson`; nothing on macOS links it yet |
| lz4 | BSD | Present only as Windows `.lib` blobs and headers under `src/dbg/lz4`; nothing on macOS links it yet |
| yara | BSD-3-Clause | Not vendored. The only trace in the tree is an icon, `src/gui/icons/yara.png` |
| Zydis | MIT | Vendored at `src/zydis_wrapper`; the `Zydis/` third-party copy proper is not otherwise present. Scheduled for removal at milestone 6, when the Capstone tokenizer replaces it |
| DWARF parser | to be decided | Not present. Decision deferred to milestone 7 |

Qt under LGPL in a macOS `.app` needs the usual relinking and attribution care.

## What no longer ships

TitanEngine, GleeBug, XEDParse and Scylla were removed at milestone 0. Zydis is scheduled for
removal at milestone 6, when the Capstone tokenizer replaces it. All five remain credited in `CREDITS.md`.

## Adding a dependency

Add its row to the table above in the same commit that introduces it. `scripts/check-credits.sh`
is the enforcement.
