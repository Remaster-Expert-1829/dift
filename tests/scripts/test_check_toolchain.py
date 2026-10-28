"""Contract tests for exact toolchain version parsing."""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path



def load_checker():
    checker = Path(__file__).resolve().parents[2] / "scripts" / "check-toolchain.py"
    specification = importlib.util.spec_from_file_location("check_toolchain", checker)
    assert specification is not None and specification.loader is not None
    module = importlib.util.module_from_spec(specification)
    sys.modules[specification.name] = module
    specification.loader.exec_module(module)
    return module


check_toolchain = load_checker()


VALID_OUTPUTS = {
    "cmake": "cmake version 3.28.3\n",
    "ninja": "1.11.1\n",
    "gcc-13": "13.3.0\n",
    "clang-18": "Ubuntu clang version 18.1.3 (1ubuntu1)\n",
    "clang-tidy-18": "Ubuntu LLVM version 18.1.3\n",
    "clang-format-18": "Ubuntu clang-format version 18.1.3 (1ubuntu1)\n",
}


class ToolchainVersionTests(unittest.TestCase):
    def test_exact_reviewed_versions_are_accepted(self) -> None:
        for tool_name, output in VALID_OUTPUTS.items():
            with self.subTest(tool=tool_name):
                check_toolchain.validate_tool(tool_name, output)

    def test_superstring_versions_are_rejected(self) -> None:
        deceptive = {
            "cmake": "cmake version 3.28.30\n",
            "ninja": "1.11.10\n",
            "gcc-13": "13.3.00\n",
            "clang-18": "Ubuntu clang version 18.1.30 (1ubuntu1)\n",
            "clang-tidy-18": "Ubuntu LLVM version 18.1.30\n",
            "clang-format-18": "Ubuntu clang-format version 18.1.30 (1ubuntu1)\n",
        }
        for tool_name, output in deceptive.items():
            with self.subTest(tool=tool_name):
                with self.assertRaisesRegex(ValueError, "drifted"):
                    check_toolchain.validate_tool(tool_name, output)

    def test_unrecognized_vendor_format_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "unrecognized"):
            check_toolchain.parse_version("cmake", "cmake v3.28.3")


if __name__ == "__main__":
    unittest.main()
