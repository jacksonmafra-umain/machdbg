from __future__ import annotations

import ctypes
from ctypes import wintypes
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional

from pywinauto import Desktop
from pywinauto.controls.uiawrapper import UIAWrapper
from pywinauto.uia_element_info import UIAElementInfo


@dataclass
class WindowMatch:
    index: int
    title: str
    window: UIAWrapper


def wrap_uia_element(element) -> UIAWrapper:
    """Wrap a raw IUIAutomationElement returned by a UIA pattern."""
    return UIAWrapper(UIAElementInfo(element))


def iter_descendants(element: UIAWrapper) -> Iterable[UIAWrapper]:
    try:
        for child in element.children():
            yield child
            yield from iter_descendants(child)
    except Exception:
        return


def supports_grid(element: UIAWrapper) -> bool:
    try:
        _ = element.iface_grid
        return True
    except Exception:
        return False


def _process_executable(process_id: int) -> Optional[str]:
    process_query_limited_information = 0x1000
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.QueryFullProcessImageNameW.argtypes = [
        wintypes.HANDLE,
        wintypes.DWORD,
        wintypes.LPWSTR,
        ctypes.POINTER(wintypes.DWORD),
    ]
    kernel32.QueryFullProcessImageNameW.restype = wintypes.BOOL
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]

    process = kernel32.OpenProcess(
        process_query_limited_information, False, process_id
    )
    if not process:
        return None
    try:
        capacity = wintypes.DWORD(32768)
        buffer = ctypes.create_unicode_buffer(capacity.value)
        if not kernel32.QueryFullProcessImageNameW(
            process, 0, buffer, ctypes.byref(capacity)
        ):
            return None
        return Path(buffer.value).name.lower()
    finally:
        kernel32.CloseHandle(process)


def list_matching_windows(title_hint: Optional[str]) -> list[WindowMatch]:
    desktop = Desktop(backend="uia")
    debugger_executables = {"x64dbg.exe", "x32dbg.exe"}
    title_hint_lower = title_hint.lower() if title_hint else None
    windows = []
    for window in desktop.windows():
        try:
            executable = _process_executable(window.element_info.process_id)
        except (OSError, ValueError):
            continue
        if executable not in debugger_executables:
            continue
        title = window.window_text() or ""
        if title_hint_lower and title_hint_lower not in title.lower():
            continue
        windows.append(window)

    # UIA desktop enumeration order is not stable between calls. Both the CLI
    # validation and select_window() enumerate, so sort before assigning indexes
    # or --window-index can select a different process than --list-windows showed.
    windows.sort(
        key=lambda window: (
            int(window.element_info.process_id),
            window.window_text() or "",
        )
    )
    return [
        WindowMatch(index=index, title=window.window_text(), window=window)
        for index, window in enumerate(windows)
    ]


def select_window(
    title_hint: Optional[str],
    window_index: Optional[int],
) -> Optional[UIAWrapper]:
    matches = list_matching_windows(title_hint)
    if not matches:
        return None
    if window_index is not None:
        if 0 <= window_index < len(matches):
            return matches[window_index].window
        return None
    if len(matches) == 1:
        return matches[0].window

    best_window = None
    best_score = -1
    for match in matches:
        score = 0
        count = 0
        for element in iter_descendants(match.window):
            count += 1
            if element.element_info.control_type in {"Table", "DataGrid"}:
                score += 10
            if supports_grid(element):
                score += 10
            if element.element_info.control_type == "List":
                score += 1
        if score > best_score or (score == best_score and count > 0):
            best_score = score
            best_window = match.window
    return best_window or matches[0].window


def find_tables(root: UIAWrapper, name_hint: Optional[str]) -> list[UIAWrapper]:
    tables = []
    for element in iter_descendants(root):
        if element.element_info.control_type in {"Table", "DataGrid"} or supports_grid(
            element
        ):
            tables.append(element)
    if not tables:
        return []
    if name_hint:
        name_hint_lower = name_hint.lower()
        return [
            table
            for table in tables
            if name_hint_lower in (table.element_info.name or "").lower()
        ]
    return tables


def find_lists(root: UIAWrapper, name_hint: Optional[str]) -> list[UIAWrapper]:
    lists = []
    for element in iter_descendants(root):
        if element.element_info.control_type == "List":
            lists.append(element)
    if not lists:
        return []
    if name_hint:
        name_hint_lower = name_hint.lower()
        return [
            lst
            for lst in lists
            if name_hint_lower in (lst.element_info.name or "").lower()
        ]
    return lists
