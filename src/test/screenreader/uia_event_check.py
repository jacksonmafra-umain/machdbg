from __future__ import annotations

import argparse
import ctypes
import re
import sys
from dataclasses import dataclass
from typing import Any, Optional

# Let pywinauto establish the process COM apartment before importing comtypes;
# reversing this order makes pywinauto warn and switch apartment assumptions.
from pywinauto.controls.uiawrapper import UIAWrapper
from pywinauto.uia_defines import IUIA

from ui_common import list_matching_windows, select_window

import comtypes
import comtypes.client
import comtypes.gen.UIAutomationClient as UIA


LOG_MARKER = "X64DBG_ACCESSIBILITY_EVENT_PROBE"
GENERAL_REGISTER = re.compile(r"^(?:EAX|RAX) = ", re.IGNORECASE)
INSTRUCTION_POINTER = re.compile(r"^(?:EIP|RIP) = ", re.IGNORECASE)


@dataclass(frozen=True)
class PropertyEvent:
    property_id: int
    name: str
    value: str
    control_type: int


def _variant_text(value: Any) -> str:
    try:
        unwrapped = value.value
    except Exception:
        unwrapped = value
    return "" if unwrapped is None else str(unwrapped)


class PropertyChangedHandler(comtypes.COMObject):
    _com_interfaces_ = [UIA.IUIAutomationPropertyChangedEventHandler]

    def __init__(self) -> None:
        super().__init__()
        self.events: list[PropertyEvent] = []

    def HandlePropertyChangedEvent(
        self, sender: Any, property_id: int, new_value: Any
    ) -> int:
        try:
            name = str(sender.CurrentName or "")
        except Exception:
            name = ""
        try:
            control_type = int(sender.CurrentControlType)
        except Exception:
            control_type = 0
        self.events.append(
            PropertyEvent(
                property_id=int(property_id),
                name=name,
                value=_variant_text(new_value),
                control_type=control_type,
            )
        )
        return 0


def _command_line(window: UIAWrapper) -> Optional[UIAWrapper]:
    for edit in window.descendants(control_type="Edit"):
        try:
            parent = edit.parent()
            if parent and parent.element_info.name == "CommandBar":
                return edit
        except Exception:
            continue
    return None


def _execute(command_line: UIAWrapper, command: str) -> None:
    command_line.set_focus()
    command_line.set_edit_text(command)
    command_line.type_keys("{ENTER}")


def _has_value(events: list[PropertyEvent], predicate) -> bool:
    return any(
        event.property_id == UIA.UIA_ValueValuePropertyId and predicate(event.value)
        for event in events
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Verify the live UIA events consumed by screen readers. The selected "
            "debugger must be paused with a live debug target."
        )
    )
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
        "--timeout",
        type=float,
        default=6.0,
        help="Seconds to collect events after issuing a single-step command.",
    )
    args = parser.parse_args()

    matches = list_matching_windows(args.window_title)
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
    print(f"Window: {window.window_text()}")
    command_line = _command_line(window)
    if command_line is None:
        print("ERROR: Could not find the debugger CommandBar edit.", file=sys.stderr)
        return 3

    automation = IUIA().iuia
    handler = PropertyChangedHandler()
    properties = (ctypes.c_long * 1)(UIA.UIA_ValueValuePropertyId)
    root = window.element_info.element
    automation.AddPropertyChangedEventHandlerNativeArray(
        root,
        UIA.TreeScope_Subtree,
        None,
        handler,
        properties,
        len(properties),
    )
    try:
        _execute(command_line, f'log "{LOG_MARKER}"')
        comtypes.client.PumpEvents(1.0)
        # Exercise a non-IP register explicitly, then restore it. A single step
        # is only guaranteed to change the instruction pointer.
        _execute(command_line, "cax=cax^1")
        comtypes.client.PumpEvents(0.5)
        _execute(command_line, "cax=cax^1")
        comtypes.client.PumpEvents(0.5)
        _execute(command_line, "sti")
        comtypes.client.PumpEvents(max(args.timeout, 0.1))
    finally:
        automation.RemovePropertyChangedEventHandler(root, handler)

    checks = [
        (
            "log message",
            _has_value(handler.events, lambda value: LOG_MARKER in value),
        ),
        (
            "running debug state",
            _has_value(handler.events, lambda value: value == "Running"),
        ),
        (
            "paused debug state",
            _has_value(handler.events, lambda value: value == "Paused"),
        ),
        (
            "general-purpose register",
            _has_value(
                handler.events, lambda value: bool(GENERAL_REGISTER.match(value))
            ),
        ),
        (
            "instruction pointer",
            _has_value(
                handler.events, lambda value: bool(INSTRUCTION_POINTER.match(value))
            ),
        ),
    ]

    failed = False
    for name, passed in checks:
        print(f"[{'OK' if passed else 'FAIL'}] {name} ValueChanged event")
        failed = failed or not passed
    print(f"Captured {len(handler.events)} UIA property-change events.")

    if failed:
        print("Observed ValueChanged payloads:")
        for event in handler.events:
            if event.property_id == UIA.UIA_ValueValuePropertyId:
                print(f"  {event.control_type} {event.name!r}: {event.value!r}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
