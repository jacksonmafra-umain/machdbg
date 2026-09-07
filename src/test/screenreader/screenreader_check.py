from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass
from typing import Optional

from pywinauto import Desktop
from pywinauto.controls.uiawrapper import UIAWrapper

from ui_common import (
    find_tables,
    list_matching_windows,
    select_window,
    wrap_uia_element,
)


@dataclass
class CheckResult:
    ok: bool
    details: str


def _runtime_id(control: UIAWrapper) -> tuple[int, ...]:
    runtime_id = control.element_info.runtime_id
    if runtime_id is None:
        return ()
    return tuple(int(part) for part in runtime_id)


def _same_element(left: UIAWrapper, right: UIAWrapper) -> bool:
    left_id = _runtime_id(left)
    return bool(left_id) and left_id == _runtime_id(right)


def _is_offscreen(control: UIAWrapper) -> bool:
    try:
        return not bool(control.element_info.visible)
    except Exception:
        return True


def _has_nonempty_rect(control: UIAWrapper) -> bool:
    try:
        rect = control.rectangle()
        return rect.width() > 0 and rect.height() > 0
    except Exception:
        return False


def _try_get_grid_dimensions(table: UIAWrapper) -> Optional[tuple[int, int]]:
    try:
        grid = table.iface_grid
        return int(grid.CurrentRowCount), int(grid.CurrentColumnCount)
    except Exception:
        return None


def _column_headers(table: UIAWrapper) -> list[UIAWrapper]:
    headers = []
    try:
        header_array = table.iface_table.GetCurrentColumnHeaders()
        headers = [
            wrap_uia_element(header_array.GetElement(index))
            for index in range(int(header_array.Length))
        ]
    except Exception:
        pass
    if not headers:
        try:
            headers = [
                child
                for child in table.children()
                if child.element_info.control_type in {"Header", "HeaderItem"}
            ]
        except Exception:
            pass
    headers.sort(key=lambda header: header.rectangle().left)
    return headers


def _enumerate_grid(
    table: UIAWrapper, max_cells: int
) -> tuple[Optional[list[list[UIAWrapper]]], CheckResult]:
    dims = _try_get_grid_dimensions(table)
    if dims is None:
        return None, CheckResult(False, "Grid pattern is not available.")

    rows, cols = dims
    if rows < 0 or cols < 0:
        return None, CheckResult(False, f"Negative Grid dimensions: {rows}x{cols}.")
    if rows * cols > max_cells:
        return None, CheckResult(
            False,
            f"Grid has {rows * cols} cells, above --max-cells={max_cells}.",
        )

    grid = table.iface_grid
    items: list[list[UIAWrapper]] = []
    runtime_ids: set[tuple[int, ...]] = set()
    failures: list[str] = []
    for row in range(rows):
        row_items = []
        for col in range(cols):
            try:
                item = wrap_uia_element(grid.GetItem(row, col))
                repeated = wrap_uia_element(grid.GetItem(row, col))
                item_id = _runtime_id(item)
                if not item_id:
                    failures.append(f"({row},{col}) has no runtime ID")
                elif item_id in runtime_ids:
                    failures.append(f"({row},{col}) reuses runtime ID {item_id}")
                else:
                    runtime_ids.add(item_id)
                if not _same_element(item, repeated):
                    failures.append(f"({row},{col}) is not stable across GetItem calls")
                if item.element_info.control_type != "DataItem":
                    failures.append(
                        f"({row},{col}) has type {item.element_info.control_type!r}, "
                        "expected 'DataItem'"
                    )

                grid_item = item.iface_grid_item
                actual_row = int(grid_item.CurrentRow)
                actual_col = int(grid_item.CurrentColumn)
                if (actual_row, actual_col) != (row, col):
                    failures.append(
                        f"({row},{col}) reports GridItem ({actual_row},{actual_col})"
                    )
                containing_grid = wrap_uia_element(grid_item.CurrentContainingGrid)
                if not _same_element(containing_grid, table):
                    failures.append(f"({row},{col}) reports the wrong containing Grid")
                row_items.append(item)
            except Exception as exc:
                failures.append(f"({row},{col}) failed: {exc}")
        items.append(row_items)

    if failures:
        shown = failures[:8]
        if len(failures) > len(shown):
            shown.append(f"... and {len(failures) - len(shown)} more")
        return items, CheckResult(False, "; ".join(shown))
    return items, CheckResult(
        True,
        f"Grid contract OK ({rows} rows, {cols} columns, {rows * cols} stable cells).",
    )


def _check_headers(
    table: UIAWrapper, grid_items: list[list[UIAWrapper]]
) -> tuple[list[UIAWrapper], CheckResult]:
    dims = _try_get_grid_dimensions(table)
    if dims is None:
        return [], CheckResult(False, "Grid pattern is not available.")
    rows, cols = dims
    headers = _column_headers(table)

    # Qt's UIA Table provider obtains headers through a cell, so an empty table
    # cannot report them through TablePattern even though structural children may
    # exist. Once a data row exists, there must be exactly one header per column.
    if rows > 0 and len(headers) != cols:
        return headers, CheckResult(
            False,
            f"TablePattern returned {len(headers)} column headers for {cols} columns.",
        )

    failures = []
    header_ids: set[tuple[int, ...]] = set()
    for index, header in enumerate(headers):
        header_id = _runtime_id(header)
        if not header_id:
            failures.append(f"Header {index} has no runtime ID")
        elif header_id in header_ids:
            failures.append(f"Header {index} reuses runtime ID {header_id}")
        else:
            header_ids.add(header_id)
        if header.element_info.control_type not in {"Header", "HeaderItem"}:
            failures.append(
                f"Header {index} has type {header.element_info.control_type!r}"
            )

    if rows > 0:
        for col, item in enumerate(grid_items[0]):
            try:
                header_array = item.iface_table_item.GetCurrentColumnHeaderItems()
                cell_headers = [
                    wrap_uia_element(header_array.GetElement(index))
                    for index in range(int(header_array.Length))
                ]
                if len(cell_headers) != 1 or not _same_element(
                    cell_headers[0], headers[col]
                ):
                    failures.append(
                        f"Cell (0,{col}) does not reference column header {col}"
                    )
            except Exception as exc:
                failures.append(f"Cell (0,{col}) header relation failed: {exc}")

    row_headers: list[UIAWrapper] = []
    if rows > 0 and cols > 0:
        try:
            row_header_array = table.iface_table.GetCurrentRowHeaders()
            row_headers = [
                wrap_uia_element(row_header_array.GetElement(index))
                for index in range(int(row_header_array.Length))
            ]
        except Exception as exc:
            failures.append(f"Table row-header query failed: {exc}")
        if len(row_headers) != rows:
            failures.append(
                f"TablePattern returned {len(row_headers)} row headers for {rows} rows"
            )
        row_header_ids: set[tuple[int, ...]] = set()
        for row, row_header in enumerate(row_headers):
            row_header_id = _runtime_id(row_header)
            if not row_header_id:
                failures.append(f"Row header {row} has no runtime ID")
            elif row_header_id in row_header_ids:
                failures.append(f"Row header {row} reuses runtime ID {row_header_id}")
            else:
                row_header_ids.add(row_header_id)
            if row_header.element_info.control_type not in {"Header", "HeaderItem"}:
                failures.append(
                    f"Row header {row} has type "
                    f"{row_header.element_info.control_type!r}"
                )
            if row < len(grid_items) and grid_items[row]:
                if _same_element(row_header, grid_items[row][0]):
                    failures.append(f"Row header {row} reuses its first data cell")
                for col, item in enumerate(grid_items[row]):
                    try:
                        header_array = item.iface_table_item.GetCurrentRowHeaderItems()
                        cell_headers = [
                            wrap_uia_element(header_array.GetElement(index))
                            for index in range(int(header_array.Length))
                        ]
                        if len(cell_headers) != 1 or not _same_element(
                            cell_headers[0], row_header
                        ):
                            failures.append(
                                f"Cell ({row},{col}) does not reference row header {row}"
                            )
                    except Exception as exc:
                        failures.append(
                            f"Cell ({row},{col}) row-header relation failed: {exc}"
                        )

    if failures:
        return headers, CheckResult(False, "; ".join(failures[:8]))
    if rows == 0:
        return headers, CheckResult(
            True, "Header relation check skipped because the Grid has no data rows."
        )
    return headers, CheckResult(
        True,
        f"Header relations OK ({len(headers)} column, {len(row_headers)} row).",
    )


def _check_child_navigation(
    table: UIAWrapper,
    grid_items: list[list[UIAWrapper]],
    headers: list[UIAWrapper],
) -> CheckResult:
    expected_controls = [item for row in grid_items for item in row] + headers
    expected_by_id = {_runtime_id(item): item for item in expected_controls}
    try:
        direct_children = table.children()
    except Exception as exc:
        return CheckResult(False, f"Failed to enumerate direct children: {exc}")

    failures = []
    actual_ids: set[tuple[int, ...]] = set()
    for child in direct_children:
        child_id = _runtime_id(child)
        if not child_id:
            failures.append("A direct child has no runtime ID")
        elif child_id in actual_ids:
            failures.append(f"Direct-child runtime ID {child_id} is duplicated")
        else:
            actual_ids.add(child_id)
        if child_id not in expected_by_id:
            failures.append(
                f"Unexpected direct child {child.element_info.control_type}/"
                f"{child.element_info.name!r}"
            )
        try:
            if not _same_element(child.parent(), table):
                failures.append(f"Child {child_id} does not point back to the table")
        except Exception as exc:
            failures.append(f"Child {child_id} parent lookup failed: {exc}")

    # Qt's Windows bridge deliberately skips children whose QAccessible state is
    # invisible. Every non-offscreen element with non-empty bounds must still be
    # reachable through normal UIA child navigation.
    for item in expected_controls:
        if _is_offscreen(item) or not _has_nonempty_rect(item):
            continue
        item_id = _runtime_id(item)
        if item_id not in actual_ids:
            failures.append(f"Visible element {item_id} is missing from child navigation")

    if failures:
        shown = failures[:8]
        if len(failures) > len(shown):
            shown.append(f"... and {len(failures) - len(shown)} more")
        return CheckResult(False, "; ".join(shown))
    return CheckResult(
        True,
        f"Child navigation OK ({len(direct_children)} exposed children).",
    )


def _check_hit_testing(
    table: UIAWrapper,
    grid_items: list[list[UIAWrapper]],
    headers: list[UIAWrapper],
) -> CheckResult:
    candidates = [
        header
        for header in headers
        if not _is_offscreen(header) and _has_nonempty_rect(header)
    ]
    if grid_items:
        candidates.extend(
            item
            for item in grid_items[0]
            if not _is_offscreen(item) and _has_nonempty_rect(item)
        )
    if not candidates:
        return CheckResult(True, "Hit-testing skipped: no visible headers or cells.")

    try:
        table.top_level_parent().set_focus()
    except Exception:
        pass
    time.sleep(0.05)

    desktop = Desktop(backend="uia")
    failures = []
    for candidate in candidates:
        rect = candidate.rectangle()
        point = (rect.left + rect.width() // 2, rect.top + rect.height() // 2)
        try:
            hit = desktop.from_point(*point)
        except Exception as exc:
            failures.append(f"{_runtime_id(candidate)} hit-test failed: {exc}")
            continue
        if hit is None or not _same_element(hit, candidate):
            hit_description = (
                "None"
                if hit is None
                else f"{hit.element_info.control_type}/{hit.element_info.name!r}"
            )
            failures.append(
                f"{_runtime_id(candidate)} hit-tested as {hit_description}"
            )

    if failures:
        return CheckResult(False, "; ".join(failures[:8]))
    return CheckResult(True, f"Exact hit-testing OK ({len(candidates)} elements).")


def _check_names(
    grid_items: list[list[UIAWrapper]], headers: list[UIAWrapper]
) -> CheckResult:
    controls = [item for row in grid_items for item in row] + headers
    nonempty = 0
    failures = []
    for control in controls:
        try:
            name = control.element_info.name
            if name:
                nonempty += 1
        except Exception as exc:
            failures.append(f"{_runtime_id(control)} Name query failed: {exc}")
    if failures:
        return CheckResult(False, "; ".join(failures[:8]))
    return CheckResult(
        True,
        f"Name properties readable ({nonempty}/{len(controls)} currently non-empty).",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="x64dbg UIA accessibility checks.")
    parser.add_argument(
        "--window-title",
        default=None,
        help="Substring to match an x64dbg/x32dbg window title (optional).",
    )
    parser.add_argument(
        "--window-index",
        type=int,
        default=None,
        help="Select a specific matching debugger window by index (0-based).",
    )
    parser.add_argument(
        "--list-windows",
        action="store_true",
        help="List matching debugger windows and exit.",
    )
    parser.add_argument(
        "--table-name",
        default=None,
        help="Substring to match one or more UIA Table names (optional).",
    )
    parser.add_argument(
        "--max-cells",
        type=int,
        default=50000,
        help="Safety limit for Grid cells checked per table.",
    )
    args = parser.parse_args()

    matches = list_matching_windows(args.window_title)
    if args.list_windows:
        if not matches:
            print("No matching debugger windows found.", file=sys.stderr)
            return 1
        for match in matches:
            print(f"{match.index}\t{match.title}")
        return 0

    if not matches:
        print("ERROR: Could not find a running x64dbg/x32dbg window.", file=sys.stderr)
        return 2
    if args.window_index is not None and not (0 <= args.window_index < len(matches)):
        print("ERROR: --window-index is out of range.", file=sys.stderr)
        return 2

    window = select_window(args.window_title, args.window_index)
    if window is None:
        print("ERROR: Could not select a debugger window.", file=sys.stderr)
        return 2

    tables = find_tables(window, args.table_name)
    if not tables:
        print("ERROR: Could not find a matching Table control.", file=sys.stderr)
        return 3

    print(f"Window: {window.window_text()}")
    failed = False
    for table in tables:
        print(f"\nTable: {table.element_info.name or '<unnamed>'}")
        grid_items, grid_result = _enumerate_grid(table, args.max_cells)
        results = [("grid", grid_result)]
        if grid_items is not None and grid_result.ok:
            headers, header_result = _check_headers(table, grid_items)
            results.extend(
                [
                    ("headers", header_result),
                    (
                        "child-navigation",
                        _check_child_navigation(table, grid_items, headers),
                    ),
                    ("hit-test", _check_hit_testing(table, grid_items, headers)),
                    ("names", _check_names(grid_items, headers)),
                ]
            )

        for name, result in results:
            status = "OK" if result.ok else "FAIL"
            print(f"[{status}] {name}: {result.details}")
            failed = failed or not result.ok

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
