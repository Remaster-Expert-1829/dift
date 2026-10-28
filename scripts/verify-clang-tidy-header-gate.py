#!/usr/bin/env python3
"""Prove that the checked-in clang-tidy policy rejects a project-header violation."""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    clang_tidy = shutil.which("clang-tidy-18")
    if clang_tidy is None:
        print("clang-tidy-18 is required to verify the project-header gate", file=sys.stderr)
        return 1
    header_root = ROOT / "include" / "spoink" / "support"
    with tempfile.TemporaryDirectory(dir=header_root, prefix="tidy-negative-") as temporary_directory:
        header = Path(temporary_directory) / "naming_violation.h"
        header.write_text("#pragma once\n\nclass bad_type {};\n", encoding="utf-8")
        completed = subprocess.run(
            [
                clang_tidy,
                "--config-file=" + str(ROOT / ".clang-tidy"),
                "--checks=readability-identifier-naming",
                "--warnings-as-errors=*",
                str(header),
                "--",
                "-std=c++17",
            ],
            cwd=ROOT,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
    if completed.returncode == 0 or "readability-identifier-naming" not in completed.stdout:
        print("clang-tidy did not reject the deliberate project-header naming violation", file=sys.stderr)
        print(completed.stdout, file=sys.stderr)
        return 1
    print("Verified clang-tidy rejects a deliberate project-header naming violation.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
