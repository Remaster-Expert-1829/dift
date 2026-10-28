#!/usr/bin/env python3
"""Fail when the supported M0 Linux tools drift from reviewed versions."""

from __future__ import annotations

import re
import subprocess
import sys
from dataclasses import dataclass
from typing import Sequence


@dataclass(frozen=True)
class Tool:
    """One reviewed executable and its authoritative version-output format."""

    command: tuple[str, ...]
    expected_version: str
    version_pattern: re.Pattern[str]


TOOLS: dict[str, Tool] = {
    "cmake": Tool(("cmake", "--version"), "3.28.3", re.compile(r"^cmake version (\d+\.\d+\.\d+)$", re.MULTILINE)),
    "ninja": Tool(("ninja", "--version"), "1.11.1", re.compile(r"^(\d+\.\d+\.\d+)$", re.MULTILINE)),
    "gcc-13": Tool(
        ("gcc-13", "-dumpfullversion", "-dumpversion"), "13.3.0", re.compile(r"^(\d+\.\d+\.\d+)$", re.MULTILINE)
    ),
    "clang-18": Tool(
        ("clang-18", "--version"), "18.1.3", re.compile(r"^(?:Ubuntu )?clang version (\d+\.\d+\.\d+)(?:\s|$)", re.MULTILINE)
    ),
    "clang-tidy-18": Tool(
        ("clang-tidy-18", "--version"), "18.1.3", re.compile(r"^(?:Ubuntu )?LLVM version (\d+\.\d+\.\d+)(?:\s|$)", re.MULTILINE)
    ),
    "clang-format-18": Tool(
        ("clang-format-18", "--version"), "18.1.3", re.compile(r"^(?:Ubuntu )?clang-format version (\d+\.\d+\.\d+)(?:\s|$)", re.MULTILINE)
    ),
}


def parse_version(tool_name: str, output: str) -> str:
    """Return precisely the vendor-reported version for a reviewed tool."""

    tool = TOOLS[tool_name]
    match = tool.version_pattern.search(output.strip())
    if match is None:
        first_line = output.splitlines()[0] if output.splitlines() else "<no output>"
        raise ValueError(f"{tool_name} emitted an unrecognized version format: {first_line}")
    return match.group(1)


def validate_tool(tool_name: str, output: str) -> None:
    """Raise ValueError unless *output* reports exactly the reviewed version."""

    actual = parse_version(tool_name, output)
    expected = TOOLS[tool_name].expected_version
    if actual != expected:
        raise ValueError(f"{tool_name} drifted: expected {expected}, got {actual}")


def command_output(command: Sequence[str]) -> str:
    return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT)


def main() -> int:
    failures: list[str] = []
    for tool_name, tool in TOOLS.items():
        try:
            validate_tool(tool_name, command_output(tool.command))
        except (OSError, subprocess.CalledProcessError, ValueError) as error:
            failures.append(str(error))

    if failures:
        print("Toolchain validation failed:", file=sys.stderr)
        print("\n".join(f"- {failure}" for failure in failures), file=sys.stderr)
        return 1
    print("Validated exact M0 toolchain versions.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
