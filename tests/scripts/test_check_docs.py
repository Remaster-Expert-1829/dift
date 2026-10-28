"""Focused tests for Markdown link, anchor, and artifact-ID validation."""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path



def load_checker():
    checker = Path(__file__).resolve().parents[2] / "scripts" / "check-docs.py"
    specification = importlib.util.spec_from_file_location("check_docs", checker)
    assert specification is not None and specification.loader is not None
    module = importlib.util.module_from_spec(specification)
    sys.modules[specification.name] = module
    specification.loader.exec_module(module)
    return module


check_docs = load_checker()


class DocumentationValidationTests(unittest.TestCase):
    def validate(self, documents: dict[str, str]) -> list[str]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            docs = root / "docs"
            docs.mkdir()
            prefixes = " ".join(f"`{prefix}`" for prefix in sorted(check_docs.REQUIRED_ID_PREFIXES))
            (docs / "README.md").write_text(f"# Documentation\n\n{prefixes}\n", encoding="utf-8")
            for name, content in documents.items():
                (docs / name).write_text(content, encoding="utf-8")
            return check_docs.validate(docs, root)

    def test_valid_definitions_references_range_and_wildcard(self) -> None:
        failures = self.validate(
            {
                "requirements.md": "> Status: Approved\n\n### FR-001 — First\n\n### FR-002 — Second\n",
                "trace.md": "> Status: Approved\n\nFR-001–002 and TEST-UNIT-CDC-* are covered.\n[requirements](requirements.md#fr-001--first)\n",
            }
        )
        self.assertEqual([], failures)

    def test_duplicate_definition_is_rejected(self) -> None:
        failures = self.validate(
            {
                "one.md": "> Status: Approved\n\n### FR-001 — First\n",
                "two.md": "> Status: Approved\n\n### FR-001 — Duplicate\n",
            }
        )
        self.assertTrue(any("duplicate artifact ID FR-001" in failure for failure in failures))

    def test_dangling_reference_is_rejected(self) -> None:
        failures = self.validate({"document.md": "> Status: Approved\n\nFR-999 must exist.\n"})
        self.assertTrue(any("dangling artifact ID FR-999" in failure for failure in failures))

    def test_malformed_identifier_is_rejected(self) -> None:
        failures = self.validate({"document.md": "> Status: Approved\n\nFR-1 is malformed.\n"})
        self.assertTrue(any("malformed artifact ID: FR-1" in failure for failure in failures), failures)

    def test_missing_local_file_is_rejected(self) -> None:
        failures = self.validate({"document.md": "> Status: Approved\n\n[missing](missing.md)\n"})
        self.assertTrue(any("missing local target" in failure for failure in failures))

    def test_missing_local_anchor_is_rejected(self) -> None:
        failures = self.validate(
            {
                "document.md": "> Status: Approved\n\n# Existing\n\n[missing](document.md#not-here)\n",
            }
        )
        self.assertTrue(any("missing anchor" in failure for failure in failures))


if __name__ == "__main__":
    unittest.main()
