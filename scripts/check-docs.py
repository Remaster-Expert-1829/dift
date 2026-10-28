#!/usr/bin/env python3
"""Validate local Markdown links, anchors, and documented artifact identifiers."""

from __future__ import annotations

import re
import sys
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
LINK = re.compile(r"(?<!!)\[[^]]*\]\(([^)]+)\)")
HEADING = re.compile(r"^(#{1,6})\s+(.+?)\s*$", re.MULTILINE)
REQUIRED_ID_PREFIXES = {"FR", "NFR", "ADR", "ARCH", "DB", "API", "CLI", "IC", "TEST", "RISK", "CFG", "EXP"}
PREFIX = r"(?:FR|NFR|ADR|ARCH|DB|API|CLI|IC|TEST|RISK|CFG|EXP)"
NUMBER = r"\d{2,3}"
SINGLE_ID = rf"{PREFIX}(?:-[A-Z0-9]+)*-(?:{NUMBER}|\*)"
REFERENCE = re.compile(rf"(?<![A-Z0-9-])({SINGLE_ID})(?:[–-]({NUMBER}))?(?![A-Z0-9-])")
POSSIBLE_ID = re.compile(rf"({PREFIX}(?:-[A-Z0-9*]+)+(?:[–-][A-Z0-9*]+)?)")
HEADING_DEFINITION = re.compile(rf"^#{{1,6}}\s+({SINGLE_ID})(?:\s|—|-)")
LIST_DEFINITION = re.compile(rf"^\s*[-*]\s+`?({SINGLE_ID})`?\s*:")
TABLE_DEFINITION = re.compile(rf"^\s*\|\s*`?({SINGLE_ID})`?\s*\|")


def slug(title: str) -> str:
    return re.sub(r"[^a-z0-9 -]", "", title.lower()).strip().replace(" ", "-")


def is_valid_reference(text: str) -> bool:
    """Accept individual IDs plus documented `ID-001–003` / `ID-*` forms."""

    return REFERENCE.fullmatch(text) is not None


def definition_from_line(line: str) -> str | None:
    """Definitions live in headings, explicit list items, or a table's first cell."""

    for pattern in (HEADING_DEFINITION, LIST_DEFINITION, TABLE_DEFINITION):
        match = pattern.match(line)
        if match is not None:
            return match.group(1)
    return None


def expand_reference(first: str, range_end: str | None) -> list[str]:
    if range_end is None or first.endswith("-*"):
        return [first]
    prefix, first_number = first.rsplit("-", 1)
    start = int(first_number)
    end = int(range_end)
    if end < start:
        raise ValueError(f"descending artifact-ID range: {first}–{range_end}")
    width = len(first_number)
    return [f"{prefix}-{number:0{width}d}" for number in range(start, end + 1)]


def validate(docs: Path, root: Path) -> list[str]:
    """Return all validation failures for a documentation tree."""

    failures: list[str] = []
    markdown_files = sorted(docs.rglob("*.md"))
    if not markdown_files:
        return ["docs contains no Markdown files"]

    anchors: dict[Path, set[str]] = {}
    definitions: dict[str, list[Path]] = defaultdict(list)
    references: list[tuple[str, str, Path]] = []
    for document in markdown_files:
        content = document.read_text(encoding="utf-8")
        anchors[document.resolve()] = {slug(title) for _, title in HEADING.findall(content)}
        has_document_state = re.search(r"^> (Status|Review date):", content, re.MULTILINE) or "**Status:**" in content
        if document.name != "README.md" and not has_document_state:
            failures.append(f"{document.relative_to(root)} has no status or review-date declaration")

        for line_number, line in enumerate(content.splitlines(), start=1):
            definition = definition_from_line(line)
            if definition is not None:
                if definition.endswith("-*"):
                    failures.append(f"{document.relative_to(root)}:{line_number} defines wildcard artifact ID: {definition}")
                else:
                    definitions[definition].append(document)
            for candidate in POSSIBLE_ID.findall(line):
                # Repository-layout templates such as ADR-NNN-<decision>.md are file-name
                # placeholders, not artifact references or definitions.
                if re.search(rf"{re.escape(candidate)}-<[^>]+>", line):
                    continue
                if not is_valid_reference(candidate):
                    failures.append(f"{document.relative_to(root)}:{line_number} has malformed artifact ID: {candidate}")
            for match in REFERENCE.finditer(line):
                try:
                    for artifact_id in expand_reference(match.group(1), match.group(2)):
                        references.append((artifact_id, match.group(0), document))
                except ValueError as error:
                    failures.append(f"{document.relative_to(root)}:{line_number} {error}")

        for target in LINK.findall(content):
            target = target.strip()
            if not target or target.startswith(("http://", "https://", "mailto:")):
                continue
            path_text, separator, anchor = target.partition("#")
            target_path = document.resolve() if not path_text else (document.parent / path_text).resolve()
            if not target_path.is_relative_to(root) or not target_path.exists():
                failures.append(f"{document.relative_to(root)} links to missing local target: {target}")
            elif separator and anchor not in anchors.get(target_path, set()):
                failures.append(f"{document.relative_to(root)} links to missing anchor: {target}")

    for artifact_id, locations in definitions.items():
        if len(locations) > 1:
            names = ", ".join(str(location.relative_to(root)) for location in locations)
            failures.append(f"duplicate artifact ID {artifact_id}: {names}")
    known_ids = set(definitions)
    for artifact_id, original, document in references:
        if artifact_id.endswith("-*"):
            # A wildcard denotes a planned family, whose members may arrive in a later milestone.
            continue
        if artifact_id not in known_ids:
            failures.append(f"{document.relative_to(root)} references dangling artifact ID {artifact_id} ({original})")

    readme = (docs / "README.md").read_text(encoding="utf-8")
    missing_prefixes = sorted(prefix for prefix in REQUIRED_ID_PREFIXES if f"`{prefix}`" not in readme)
    if missing_prefixes:
        failures.append("docs/README.md is missing artifact-ID prefixes: " + ", ".join(missing_prefixes))
    return failures


def main() -> int:
    failures = validate(DOCS, ROOT)
    if failures:
        print("Documentation validation failed:", file=sys.stderr)
        print("\n".join(f"- {failure}" for failure in failures), file=sys.stderr)
        return 1
    print("Validated Markdown links, anchors, statuses, artifact-ID definitions, and references.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
