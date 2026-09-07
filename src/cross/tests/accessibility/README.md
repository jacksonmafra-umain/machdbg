# Cross-platform minidump accessibility capture

This is a discovery-first end-to-end test for the cross-platform `minidump`
application. It launches the real application and reads the native accessibility
API through [xa11y](https://github.com/xa11y/xa11y): UI Automation on Windows,
AXUIElement on macOS, and AT-SPI2 on Linux.

The pinned fixture is downloaded automatically:

- URL: `https://github.com/mrexodia/dumpulator/releases/download/artifacts/HarnessFull_x64.dmp`
- Size: `31,694,576` bytes
- SHA-256: `595372311c22b9ea33031f9718ab3d7310e825e1ee2a9cfef0e1d0d2fd383bad`

## What it exercises

The script activates every minidump tab and covers every interface supplied by
`CrossAccessible`:

| View | Accessible implementation |
| --- | --- |
| Memory Map | `AccessibleStdTable` |
| Dump | `AccessibleHexDump` |
| Disassembly | `AccessibleDisassembly` |
| Registers | `AccessibleRegistersView` |

This also exercises `AccessibleAbstractTableView`, its virtual data cells,
column headers, dedicated hidden row headers, native parent/child navigation,
names, bounds, focus, selection, register visual order, and model resets caused
by scrolling. The Threads `QTableWidget` is captured as
a native Qt-table comparison.

The first runs intentionally produce rich platform-specific captures. Use
`--strict` only after comparing Windows, macOS, and Linux output and deciding
which remaining differences should be contractual.

## Setup

Build the cross project first. A Ninja build normally places the executable at
`src/cross/build/minidump`; Visual Studio commonly uses
`src/cross/build/RelWithDebInfo/minidump.exe`. A macOS bundle uses
`src/cross/build/minidump.app/Contents/MacOS/minidump`. These locations are
searched automatically.

```shell
cd src/cross/tests/accessibility
uv sync
```

Run with auto-detected paths:

```shell
uv run x64dbg-minidump-accessibility
```

Or pass the executable explicitly:

```shell
uv run x64dbg-minidump-accessibility \
  --minidump ../../build/minidump \
  --out captures/linux
```

Useful options:

```text
--dump FILE       Use another DMP/PE instead of downloading the pinned fixture.
--attach-pid PID  Inspect a minidump process launched manually.
--no-scroll       Capture without synthesizing input/model resets.
--keep-open       Do not close a process launched by the script.
--strict          Turn discovery warnings into failures.
```

## Platform preparation

### Windows

UI Automation normally works without additional setup. Run the shell elevated
if the application is elevated.

### macOS

Grant the Python interpreter used by `uv` permission under **System Settings →
Privacy & Security → Accessibility**. On macOS 26, xa11y also requires **Screen
& System Audio Recording** permission. The interpreter path can be printed with:

```shell
uv run python -c "import sys; print(sys.executable)"
```

Bringing the application to the foreground before the scrolling phase may be
necessary when attaching to an already-running process.

Qt's native Cocoa bridge reshapes a table as direct `AXRow` and `AXColumn`
children, with `AXCell` elements nested under each row. Qt 6.8 and later contain
a lifetime bug in that synthesized representation: Cocoa can synchronously
replace the accessible table adapter while processing a virtual-cell event.
x64dbg's custom tables therefore follow Qt's `QTableView` accessibility
lifetime model. Cells and headers retain the QObject-backed view and re-query
its current adapter, while focus and selection events identify a cell through
the view plus a stable child index. The custom tables and the standard
`QTableWidget` Threads view consequently retain the same native row/column
semantics. This path is verified with Qt 6.9.2, Qt 6.11.1, and Qt 6.12.0 beta2.

In current Qt 6 native `QTableWidget`/`QTableView` xa11y captures, the
`AXColumn` snapshots do not include the visible column-header names. The script
records those columns and marks missing header names as known expected failures
on macOS. This keeps the gap visible and automatically turns the checks into
passes if xa11y later exposes the header relationships; data-cell Name
assertions remain active. The script also handles the `AXRadioButton` and,
under Qt 6.9, unnamed `AXUnknown` roles that Cocoa may expose for `QTabBar`
tabs.

### Linux

Run under a desktop session with AT-SPI2 available. Force Qt accessibility on:

```shell
export QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1
uv run x64dbg-minidump-accessibility --minidump ../../build/minidump
```

For a headless environment, start Xvfb, a D-Bus session, and the AT-SPI bridge.
The `xa11y/setup-a11y@v1` GitHub Action performs that setup for CI.

## Captures

The output directory contains:

- `application.txt` and `application.json`: normalized application tree.
- One text tree per selected control.
- `report.json`: rich normalized properties, platform-native `raw` fields,
  before/after scroll snapshots, checks, and accessibility events.
- `minidump.stdout.log` and `minidump.stderr.log`.

xa11y currently normalizes Qt/UIA DataItem cells as `table_row` on Windows.
The action check recognizes those native DataItems so this known normalization
bug does not prevent testing real cell selection. Linux exposes the
corresponding objects directly as `table_cell`; macOS exposes
`AXRow` containers with nested `AXCell`/`table_cell` objects. The underlying
x64dbg child layout deliberately follows Qt's native accessible table contract;
the capture records both normalized roles and native `raw` fields so these
platform adaptations remain visible.
