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
