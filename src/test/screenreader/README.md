# Screen reader checks (Windows UI Automation)

These scripts inspect a **running** x64dbg/x32dbg process through Windows UI
Automation (UIA). They are native accessibility smoke tests, not substitutes
for Narrator/NVDA testing and not currently part of CTest or CI.

## Setup

```powershell
cd c:\CodeBlocks\x64dbg\src\test\screenreader
uv sync
```

## Contract checks

1. Launch x64dbg and open/populate the table views you want to inspect.
2. Optionally resize a view, scroll it, and reorder or hide columns.
3. List the debugger windows visible to UIA:

```powershell
uv run x64dbg-screenreader-checks --list-windows
```

4. Check every table in the selected debugger window:

```powershell
uv run x64dbg-screenreader-checks
```

Or select a window/table explicitly:

```powershell
uv run x64dbg-screenreader-checks --window-index 0 --table-name "Disassembly"
uv run x64dbg-screenreader-checks --table-name "Dump 1"
```

The checker validates:

- Grid row/column counts describe data cells only; column headers are separate.
- Every `Grid.GetItem(row, column)` returns a stable, unique DataItem whose
  GridItem row/column and containing Grid round-trip correctly.
- A populated table exposes one column-header relation per column, and every
  first-row cell points to the corresponding header.
- Every row has a distinct, hidden row-header object and all cells in that row
  reference it. The first data cell is never repurposed as the header.
- Visible cells/column headers participate in direct child navigation and point
  back to the table; hidden row headers remain available through TablePattern.
- Hit-testing the center of visible headers and first-row cells returns that
  exact UIA element.
- UIA Name properties can be queried for all cells and headers.

The script exits non-zero on a contract failure. `--max-cells` limits work for
unexpectedly large grids.

## Live event checks

With a live debug target paused, the event checker logs a unique marker,
toggles and restores `CAX`, and single-steps once through the debugger Command
Bar:

```powershell
uv run x64dbg-screenreader-event-checks
```

It listens through native UIA and requires `ValueChanged` payloads for the log
message, `Running` and `Paused` debug states, a general-purpose `EAX`/`RAX`
register, and the updated `EIP`/`RIP` list item. These are the passive live-region
events consumed by Microsoft Narrator; a QLabel `NameChanged` event alone is not
announced. The command intentionally advances the debug target by one
instruction.

## Capture visible contents

The dump command writes the currently visible tables, register lists, text
views, and trees. Cell/header/register text is read from UIA Name first, matching
x64dbg's QAccessible contract.

```powershell
uv run x64dbg-ui-dump --out visible_tables.txt
uv run x64dbg-ui-dump --table-name "Dump 1" --max-rows 20 --debug
```

Useful diagnostics:

```powershell
uv run x64dbg-ui-dump --list-controls --dump-tree --tree-out uia_tree.txt
```

The dump command exits non-zero if a selected view could not be captured; the
output file includes the corresponding `ERROR:` section.

## Troubleshooting and limitations

- If x64dbg is running as Administrator, run the scripts from an elevated shell.
- Window discovery verifies the owning executable is `x64dbg.exe` or
  `x32dbg.exe`; unrelated terminals or file managers with “x64dbg” in their
  title are ignored.
- If multiple debugger windows exist, use `--list-windows` and
  `--window-index`.
- Empty tables can validate Grid structure but cannot expose header relations
  through Qt's UIA Table provider until at least one data row exists.
- Table scrolling/model-reset transitions still require a deliberate manual
  scenario. The event checker automates only one instruction step.
- macOS VoiceOver/Accessibility Inspector and Linux AT-SPI require separate
  platform-specific testing.
