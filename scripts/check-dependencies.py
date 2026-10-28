#!/usr/bin/env python3
"""Keep the M0 vcpkg baseline, test dependency, and notice synchronized."""

from __future__ import annotations

import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_BASELINE = "74e6536215718009aae747d86d84b78376bf9e09"


def main() -> int:
    manifest = json.loads((ROOT / "vcpkg.json").read_text(encoding="utf-8"))
    configuration = json.loads((ROOT / "vcpkg-configuration.json").read_text(encoding="utf-8"))
    notice = (ROOT / "third_party" / "NOTICE.md").read_text(encoding="utf-8")
    failures: list[str] = []

    if configuration.get("default-registry", {}).get("baseline") != EXPECTED_BASELINE:
        failures.append("vcpkg baseline does not match the reviewed M0 baseline")
    overrides = {entry.get("name"): entry for entry in manifest.get("overrides", [])}
    catch2 = overrides.get("catch2", {})
    if catch2.get("version-semver") != "3.11.0" or catch2.get("port-version") != 0:
        failures.append("Catch2 must be locked to vcpkg port 3.11.0#0")
    if "Catch2" not in notice or "3.11.0#0" not in notice or "BSL-1.0" not in notice:
        failures.append("third_party/NOTICE.md is missing Catch2 version or license")

    if failures:
        print("Dependency validation failed:", file=sys.stderr)
        print("\n".join(f"- {failure}" for failure in failures), file=sys.stderr)
        return 1
    print("Validated vcpkg baseline, Catch2 lock, and license notice.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
