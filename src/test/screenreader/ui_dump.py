from __future__ import annotations

import argparse
import sys
from typing import Optional

from pywinauto.controls.uiawrapper import UIAWrapper

from ui_common import (
    find_tables,
    find_lists,
    iter_descendants,
    list_matching_windows,
    select_window,
    wrap_uia_element,
)


def _dump_tree(root: UIAWrapper, max_items: int) -> list[str]:
    lines = []
    count = 0
    for element in iter_descendants(root):
        try:
            rect = element.rectangle()
            rect_text = f"({rect.left},{rect.top})-({rect.right},{rect.bottom})"
        except Exception:
            rect_text = "(n/a)"
        name = element.element_info.name or ""
        value = ""
        try:
            val = element.iface_value.CurrentValue
            if val is not None:
                value = str(val)
        except Exception:
            pass
        if not value:
            try:
                value = element.window_text() or ""
            except Exception:
                pass
        if not value:
            try:
                legacy = element.iface_legacyiaccessible
                value = legacy.CurrentName or legacy.CurrentValue or ""
            except Exception:
                pass
        ctype = element.element_info.control_type or "<unknown>"
        if value and value != name:
            lines.append(f"{ctype}\t{name}\t{value}\t{rect_text}")
        else:
            lines.append(f"{ctype}\t{name}\t{rect_text}")
        count += 1
        if count >= max_items:
            break
    return lines


def _find_main_window(
    title_hint: Optional[str], window_index: Optional[int]
) -> Optional[UIAWrapper]:
    return select_window(title_hint, window_index)


def _get_cell_text(cell: UIAWrapper) -> str:
    # QAccessible table cells, headers, and register items expose their displayed
    # text as UIA Name. Value is only a fallback for standard value controls.
    try:
        name = cell.element_info.name
        if name:
            return name
    except Exception:
        pass
    try:
        text = cell.window_text()
        if text:
            return text
    except Exception:
        pass
    try:
        value = cell.iface_value.CurrentValue
        if value is not None:
            return str(value)
    except Exception:
        pass
    try:
        legacy = cell.iface_legacyiaccessible
        legacy_value = legacy.CurrentValue
        if legacy_value:
            return legacy_value
        legacy_name = legacy.CurrentName
        if legacy_name:
            return legacy_name
    except Exception:
        pass
    return ""


def _get_control_name(control: UIAWrapper) -> str:
    try:
        name = control.element_info.name
        if name:
            return name
    except Exception:
        pass
    try:
        text = control.window_text()
        if text:
            return text
    except Exception:
        pass
    try:
        legacy = control.iface_legacyiaccessible
        legacy_name = legacy.CurrentName or legacy.CurrentValue
        if legacy_name:
            return legacy_name
    except Exception:
        pass
    return "<unnamed>"


def _rect_intersects(a, b) -> bool:
    return not (
        a.right <= b.left
        or a.left >= b.right
        or a.bottom <= b.top
        or a.top >= b.bottom
    )


def _is_offscreen(control: UIAWrapper) -> bool:
    try:
        return not bool(control.element_info.visible)
    except Exception:
        return True


def _dump_visible_table(table: UIAWrapper, max_rows: Optional[int], debug: bool) -> str:
    try:
        grid = table.iface_grid
    except Exception as exc:
        raise RuntimeError(f"UIA Grid pattern not available: {exc}") from exc

    table_rect = table.rectangle()
    rows = int(grid.CurrentRowCount)
    cols = int(grid.CurrentColumnCount)
    if debug:
        print(f"Table grid: rows={rows} cols={cols}", file=sys.stderr)

    header_items = []
    try:
        header_array = table.iface_table.GetCurrentColumnHeaders()
        header_items = [
            wrap_uia_element(header_array.GetElement(index))
            for index in range(int(header_array.Length))
        ]
    except Exception:
        pass
    if not header_items:
        for element in iter_descendants(table):
            if element.element_info.control_type in {"HeaderItem", "Header"}:
                header_items.append(element)
    header_items.sort(key=lambda item: item.rectangle().left)
    header_texts = [
        (_get_cell_text(item) or f"col{idx}") for idx, item in enumerate(header_items)
    ]
    if not header_texts:
        header_texts = [f"col{idx}" for idx in range(cols)]

    lines = ["\t".join(header_texts)]

    row_limit = rows if max_rows is None else min(rows, max_rows)
    if rows == 0 or cols == 0:
        return "\t".join(header_texts) + "\n"

    grid_usable = True
    try:
        test_item = grid.GetItem(0, 0)
        _ = wrap_uia_element(test_item)
    except Exception:
        grid_usable = False

    if not grid_usable:
        if debug:
            print(
                "Grid.GetItem unavailable; falling back to DataItem enumeration.",
                file=sys.stderr,
            )
        data_lines = _dump_visible_table_by_dataitems(
            table, table_rect, max_rows, header_texts, debug
        )
        return data_lines

    for row in range(row_limit):
        row_cells = []
        any_visible = False
        for col in range(cols):
            try:
                cell = grid.GetItem(row, col)
                cell_wrap = wrap_uia_element(cell)
                cell_rect = cell_wrap.rectangle()
                rect_valid = cell_rect.width() > 0 and cell_rect.height() > 0
                if rect_valid and _rect_intersects(cell_rect, table_rect):
                    any_visible = True
                text = _get_cell_text(cell_wrap)
                if debug and row < 2 and col < 2:
                    try:
                        legacy = cell_wrap.iface_legacyiaccessible
                        legacy_name = legacy.CurrentName
                        legacy_value = legacy.CurrentValue
                    except Exception:
                        legacy_name = None
                        legacy_value = None
                    try:
                        current_value = cell_wrap.iface_value.CurrentValue
                    except Exception:
                        current_value = None
                    print(
                        "Cell",
                        {"row": row, "col": col},
                        "ct=",
                        cell_wrap.element_info.control_type,
                        "name=",
                        cell_wrap.element_info.name,
                        "value=",
                        current_value,
                        "legacy_name=",
                        legacy_name,
                        "legacy_value=",
                        legacy_value,
                        "rect=",
                        cell_rect,
                        file=sys.stderr,
                    )
                if text and not rect_valid:
                    # Some controls omit rects but still expose visible text.
                    any_visible = True
                row_cells.append(text)
            except Exception:
                if debug and row < 2 and col < 2:
                    print(
                        f"Cell {{'row': {row}, 'col': {col}}} GetItem failed",
                        file=sys.stderr,
                    )
                row_cells.append("")
        if any_visible:
            lines.append("\t".join(row_cells))

    if len(lines) <= 1:
        if debug:
            print(
                "No grid rows captured; falling back to DataItem enumeration.",
                file=sys.stderr,
            )
        return _dump_visible_table_by_dataitems(
            table, table_rect, max_rows, header_texts, debug
        )

    return "\n".join(lines) + "\n"


def _dump_visible_table_by_dataitems(
    table: UIAWrapper,
    table_rect,
    max_rows: Optional[int],
    header_texts: list[str],
    debug: bool,
) -> str:
    items = []
    for element in iter_descendants(table):
        if element.element_info.control_type != "DataItem":
            continue
        try:
            rect = element.rectangle()
        except Exception:
            continue
        if rect.width() <= 0 or rect.height() <= 0:
            continue
        if not _rect_intersects(rect, table_rect):
            continue
        text = _get_cell_text(element)
        items.append((rect.top, rect.left, text))

    if not items:
        return "\t".join(header_texts) + "\n"

    items.sort(key=lambda item: (item[0], item[1]))

    rows: list[dict] = []
    tolerance = 2
    for top, left, text in items:
        if rows and abs(top - rows[-1]["top"]) <= tolerance:
            rows[-1]["items"].append((left, text))
        else:
            rows.append({"top": top, "items": [(left, text)]})

    if max_rows is not None:
        rows = rows[:max_rows]

    lines = []
    if header_texts:
        lines.append("\t".join(header_texts))
    for row in rows:
        row_items = sorted(row["items"], key=lambda item: item[0])
        texts = [text for _, text in row_items]
        if any(texts):
            lines.append("\t".join(texts))

    if debug:
        print(f"DataItem rows dumped: {len(rows)}", file=sys.stderr)

    return "\n".join(lines) + "\n"


def _dump_visible_list(
    list_control: UIAWrapper, max_rows: Optional[int], debug: bool
) -> str:
    list_rect = list_control.rectangle()
    items = []
    for element in iter_descendants(list_control):
        if element.element_info.control_type not in {"ListItem", "DataItem"}:
            continue
        try:
            rect = element.rectangle()
        except Exception:
            continue
        if rect.width() <= 0 or rect.height() <= 0:
            continue
        if not _rect_intersects(rect, list_rect):
            continue
        text = _get_cell_text(element)
        items.append((rect.top, rect.left, text))

    if not items:
        return ""

    items.sort(key=lambda item: (item[0], item[1]))

    rows: list[dict] = []
    tolerance = 2
    for top, left, text in items:
        if rows and abs(top - rows[-1]["top"]) <= tolerance:
            rows[-1]["items"].append((left, text))
        else:
            rows.append({"top": top, "items": [(left, text)]})

    if max_rows is not None:
        rows = rows[:max_rows]

    lines = []
    for row in rows:
        row_items = sorted(row["items"], key=lambda item: item[0])
        texts = [text for _, text in row_items]
        if any(texts):
            lines.append("\t".join(texts))

    if debug:
        print(f"List rows dumped: {len(rows)}", file=sys.stderr)

    return "\n".join(lines) + "\n"


def _dump_visible_text(text_control: UIAWrapper, max_rows: Optional[int]) -> str:
    text = ""
    try:
        val = text_control.iface_value.CurrentValue
        if val is not None:
            text = str(val)
    except Exception:
        pass
    if not text:
        try:
            text = text_control.window_text() or ""
        except Exception:
            pass
    if not text:
        try:
            legacy = text_control.iface_legacyiaccessible
            text = legacy.CurrentValue or legacy.CurrentName or ""
        except Exception:
            pass
    if not text:
        return ""
    if "\n" not in text and len(text.strip()) < 40:
        return ""
    lines = [line.rstrip() for line in text.splitlines() if line.strip()]
    if max_rows is not None:
        lines = lines[:max_rows]
    if not lines:
        return ""
    return "\n".join(lines) + "\n"


def _dump_visible_tree(tree_control: UIAWrapper, max_rows: Optional[int]) -> str:
    tree_rect = tree_control.rectangle()
    items = []
    for element in iter_descendants(tree_control):
        if element.element_info.control_type != "TreeItem":
            continue
        if _is_offscreen(element):
            continue
        try:
            rect = element.rectangle()
        except Exception:
            continue
        if rect.width() <= 0 or rect.height() <= 0:
            continue
        if not _rect_intersects(rect, tree_rect):
            continue
        text = _get_cell_text(element) or element.element_info.name or ""
        depth = 0
        parent = element
        while True:
            try:
                parent = parent.parent()
            except Exception:
                break
            if parent is None:
                break
            if parent.element_info.control_type == "Tree":
                break
            depth += 1
        items.append((rect.top, rect.left, depth, text))
    if not items:
        return ""
    items.sort(key=lambda item: (item[0], item[1]))
    lines = []
    for _, __, depth, text in items:
        if not text:
            continue
        lines.append(f"{'  ' * depth}{text}")
        if max_rows is not None and len(lines) >= max_rows:
            break
    if not lines:
        return ""
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="Dump visible UIA table contents.")
    parser.add_argument(
        "--window-title",
        default=None,
        help="Substring to match the main x64dbg window title (optional).",
    )
    parser.add_argument(
        "--window-index",
        type=int,
        default=None,
        help="Select a specific matching window by index (0-based).",
    )
    parser.add_argument(
        "--list-windows",
        action="store_true",
        help="List matching windows and exit.",
    )
    parser.add_argument(
        "--table-name",
        default=None,
        help="Substring to match the UIA Table name (optional).",
    )
    parser.add_argument(
        "--out",
        default="x64dbg_visible_table.txt",
        help="Output file path.",
    )
    parser.add_argument(
        "--max-rows",
        type=int,
        default=None,
        help="Maximum rows per table to dump (optional).",
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="Print debug info to stderr.",
    )
    parser.add_argument(
        "--list-controls",
        action="store_true",
        help="List control types under the window (debugging).",
    )
    parser.add_argument(
        "--dump-tree",
        action="store_true",
        help="Dump a flat UIA tree (debugging).",
    )
    parser.add_argument(
        "--tree-max",
        type=int,
        default=200,
        help="Maximum UIA elements to include in --dump-tree.",
    )
    parser.add_argument(
        "--tree-out",
        default="x64dbg_uia_tree.txt",
        help="Output file for --dump-tree.",
    )
    args = parser.parse_args()

    matches = list_matching_windows(args.window_title)
    windows = [match.window for match in matches]
    if args.list_windows:
        if not windows:
            print("No matching windows found.", file=sys.stderr)
            return 1
        for match in matches:
            print(f"{match.index}\t{match.title}")
        return 0

    if not windows:
        print("ERROR: Could not find a window to attach to.", file=sys.stderr)
        return 2

    if args.window_index is not None and not (0 <= args.window_index < len(windows)):
        print("ERROR: --window-index is out of range.", file=sys.stderr)
        return 2
    window = _find_main_window(args.window_title, args.window_index)
    if window is None:
        print("ERROR: Could not find a window to attach to.", file=sys.stderr)
        return 2

    tables = find_tables(window, args.table_name)
    lists = find_lists(window, args.table_name)
    texts = []
    trees = []
    for element in iter_descendants(window):
        ctype = element.element_info.control_type
        if ctype in {"Edit", "Document"}:
            texts.append(element)
        elif ctype == "Text":
            try:
                rect = element.rectangle()
                if rect.width() >= 200 and rect.height() >= 60:
                    texts.append(element)
            except Exception:
                pass
        elif ctype == "Tree":
            trees.append(element)
    if not tables and not lists and not texts and not trees:
        if args.list_controls:
            counts = {}
            for element in iter_descendants(window):
                ctype = element.element_info.control_type or "<unknown>"
                counts[ctype] = counts.get(ctype, 0) + 1
            print("No tables found. Control type counts:", file=sys.stderr)
            for ctype in sorted(counts, key=counts.get, reverse=True):
                print(f"- {ctype}: {counts[ctype]}", file=sys.stderr)
        if args.dump_tree:
            tree_lines = _dump_tree(window, args.tree_max)
            with open(args.tree_out, "w", encoding="utf-8") as handle:
                handle.write("\n".join(tree_lines) + "\n")
            print(f"Wrote {args.tree_out}")
        print(
            "ERROR: Could not find any supported controls under the debugger window.",
            file=sys.stderr,
        )
        return 3

    window_rect = window.rectangle()
    visible_tables = []
    for table in tables:
        try:
            if not table.element_info.visible:
                continue
        except Exception:
            pass
        if _is_offscreen(table):
            try:
                if not _rect_intersects(table.rectangle(), window_rect):
                    continue
            except Exception:
                continue
        try:
            if not _rect_intersects(table.rectangle(), window_rect):
                continue
        except Exception:
            continue
        visible_tables.append(table)

    visible_lists = []
    for lst in lists:
        try:
            if not lst.element_info.visible:
                continue
        except Exception:
            pass
        if _is_offscreen(lst):
            try:
                if not _rect_intersects(lst.rectangle(), window_rect):
                    continue
            except Exception:
                continue
        try:
            if not _rect_intersects(lst.rectangle(), window_rect):
                continue
        except Exception:
            continue
        visible_lists.append(lst)

    visible_texts = []
    for txt in texts:
        try:
            if not txt.element_info.visible:
                continue
        except Exception:
            pass
        if _is_offscreen(txt):
            try:
                if not _rect_intersects(txt.rectangle(), window_rect):
                    continue
            except Exception:
                continue
        try:
            if not _rect_intersects(txt.rectangle(), window_rect):
                continue
        except Exception:
            continue
        visible_texts.append(txt)

    visible_trees = []
    for tree in trees:
        try:
            if not tree.element_info.visible:
                continue
        except Exception:
            pass
        if _is_offscreen(tree):
            try:
                if not _rect_intersects(tree.rectangle(), window_rect):
                    continue
            except Exception:
                continue
        try:
            if not _rect_intersects(tree.rectangle(), window_rect):
                continue
        except Exception:
            continue
        visible_trees.append(tree)

    if (
        not visible_tables
        and not visible_lists
        and not visible_texts
        and not visible_trees
    ):
        print("ERROR: No visible controls found.", file=sys.stderr)
        return 4

    if args.dump_tree:
        tree_lines = _dump_tree(window, args.tree_max)
        with open(args.tree_out, "w", encoding="utf-8") as handle:
            handle.write("\n".join(tree_lines) + "\n")
        print(f"Wrote {args.tree_out}")

    def _table_sort_key(tbl: UIAWrapper):
        try:
            rect = tbl.rectangle()
            return (rect.top, rect.left, rect.bottom, rect.right)
        except Exception:
            return (0, 0, 0, 0)

    visible_tables.sort(key=_table_sort_key)
    visible_lists.sort(key=_table_sort_key)
    visible_texts.sort(key=_table_sort_key)
    visible_trees.sort(key=_table_sort_key)

    sections = []
    had_errors = False
    for index, table in enumerate(visible_tables, start=1):
        name = _get_control_name(table)
        try:
            table_text = _dump_visible_table(table, args.max_rows, args.debug)
        except Exception as exc:
            had_errors = True
            table_text = f"ERROR: {exc}\n"
        sections.append(f"=== View {index}: {name} ===\n{table_text}")

    for index, lst in enumerate(visible_lists, start=1):
        name = _get_control_name(lst)
        try:
            list_text = _dump_visible_list(lst, args.max_rows, args.debug)
        except Exception as exc:
            had_errors = True
            list_text = f"ERROR: {exc}\n"
        sections.append(f"=== View {len(sections) + 1}: {name} ===\n{list_text}")

    for index, txt in enumerate(visible_texts, start=1):
        name = _get_control_name(txt)
        try:
            text_body = _dump_visible_text(txt, args.max_rows)
        except Exception as exc:
            had_errors = True
            text_body = f"ERROR: {exc}\n"
        if text_body:
            sections.append(f"=== View {len(sections) + 1}: {name} ===\n{text_body}")

    for index, tree in enumerate(visible_trees, start=1):
        name = _get_control_name(tree)
        try:
            tree_body = _dump_visible_tree(tree, args.max_rows)
        except Exception as exc:
            had_errors = True
            tree_body = f"ERROR: {exc}\n"
        if tree_body:
            sections.append(f"=== View {len(sections) + 1}: {name} ===\n{tree_body}")

    with open(args.out, "w", encoding="utf-8") as handle:
        handle.write("\n".join(sections))

    print(f"Wrote {args.out} ({len(sections)} views)")
    return 1 if had_errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
