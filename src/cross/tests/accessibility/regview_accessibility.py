"""Milestone 3's on-screen proof, read through the native accessibility API.

`regview --selftest` already checks the same thing offscreen and is what CI runs (see
`docs/COMPILE-macos.md`). This script exists for what that cannot see: whether the register table
is a *table* in the accessibility tree, with the rows and values a screen reader -- or a person --
would actually find. It needs a window server and Accessibility permission for whatever runs it,
so it is a local check rather than a CI one.

    cd src/cross/tests/accessibility && uv run x64dbg-regview-accessibility \\
        --target ../../build/macos-arm64/tests/targets/run_endlessly
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

import xa11y

SCRIPT_DIR = Path(__file__).resolve().parent
HEX_WITH_A_NONZERO_DIGIT = re.compile(r"[1-9a-f]")


def default_regview_binary() -> Path | None:
    """The bundle this repository's own build produces, if it is there."""
    candidate = (
        SCRIPT_DIR
        / ".."
        / ".."
        / "build"
        / "macos-arm64"
        / "regview.app"
        / "Contents"
        / "MacOS"
        / "regview"
    ).resolve()
    return candidate if candidate.is_file() else None


def cell_values(table: Any, max_rows: int = 64) -> list[tuple[str, str]]:
    """(name, value) for each register row, read out of the table's own accessibility tree."""
    tree = table.tree(max_depth=4)
    rows: list[tuple[str, str]] = []

    def visit(node: dict[str, Any]) -> None:
        children = node.get("children") or []
        if node.get("role") == "row" or (len(children) == 2 and not children[0].get("children")):
            texts = [(child.get("value") or child.get("name") or "").strip() for child in children]
            if len(texts) == 2 and texts[0]:
                rows.append((texts[0], texts[1]))
        for child in children:
            visit(child)

    visit(tree)
    return rows[:max_rows]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--regview", type=Path, default=None, help="path to the regview binary")
    parser.add_argument("--target", type=Path, required=True, help="the target to launch")
    parser.add_argument("--out", type=Path, default=SCRIPT_DIR / "regview-capture")
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    binary = (args.regview or default_regview_binary())
    if binary is None:
        print("Could not find regview; pass --regview", file=sys.stderr)
        return 2
    binary = binary.resolve()
    target = args.target.resolve()
    if not target.is_file():
        print(f"No such target: {target}", file=sys.stderr)
        return 2

    args.out.mkdir(parents=True, exist_ok=True)
    stdout_handle = (args.out / "regview.stdout.log").open("wb")
    stderr_handle = (args.out / "regview.stderr.log").open("wb")

    process = subprocess.Popen(
        [str(binary), str(target)],
        cwd=binary.parent,
        env=os.environ.copy(),
        stdout=stdout_handle,
        stderr=stderr_handle,
    )
    print(f"Launched {binary} against {target.name} (PID {process.pid})")

    failures: list[str] = []
    try:
        app = xa11y.App.by_pid(process.pid, timeout=args.timeout)
        print(f"Attached to {app.name!r}")

        registers = app.locator('table[name="Registers"]').wait_visible(timeout=args.timeout)
        (args.out / "registers.json").write_text(
            json.dumps(registers.tree(max_depth=4), indent=2, ensure_ascii=False),
            encoding="utf-8",
        )

        # The engine fills the table on the first stop, which arrives a moment after launch.
        rows: list[tuple[str, str]] = []
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            rows = cell_values(registers)
            if any(name in ("pc", "rip") and value for name, value in rows):
                break
            time.sleep(0.25)

        print(f"{len(rows)} register row(s) visible")
        for name, value in rows:
            print(f"  {name} {value}")

        # A populated table, not merely a present one: 34 rows of zeroes is exactly what a view
        # that renders but is never fed looks like, and a row count alone would accept it.
        if len(rows) < 20:
            failures.append(f"only {len(rows)} register rows are visible")

        by_name = dict(rows)
        for register in ("pc", "rip"):
            if register in by_name:
                if not HEX_WITH_A_NONZERO_DIGIT.search(by_name[register]):
                    failures.append(f"{register} reads {by_name[register]!r}")
                break
        else:
            failures.append("neither pc nor rip is in the table")

        for register in ("sp", "rsp"):
            if register in by_name:
                if not HEX_WITH_A_NONZERO_DIGIT.search(by_name[register]):
                    failures.append(f"{register} reads {by_name[register]!r}")
                if by_name.get(register) == by_name.get("pc", by_name.get("rip")):
                    failures.append("the stack pointer and the program counter read the same")
                break
        else:
            failures.append("neither sp nor rsp is in the table")

        memory = app.locator('table[name="Memory"]').wait_visible(timeout=args.timeout)
        memory_tree = memory.tree(max_depth=3)
        (args.out / "memory.json").write_text(
            json.dumps(memory_tree, indent=2, ensure_ascii=False), encoding="utf-8"
        )
        if not (memory_tree.get("children") or []):
            failures.append("the memory panel has no rows")
    except Exception as exc:  # noqa: BLE001 -- any failure here is a failed check, not a bug
        failures.append(f"{type(exc).__name__}: {exc}")
    finally:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
        stdout_handle.close()
        stderr_handle.close()

    if failures:
        print("\nFAILED:")
        for failure in failures:
            print(f"  {failure}")
        return 1

    print("\nPASS: the register table and the memory panel are populated on screen")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
