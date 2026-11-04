#!/usr/bin/env python3
"""Keep reviewed M1 vcpkg dependencies and notices synchronized."""

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
    expected = {"openssl": ("3.6.0", "Apache-2.0"), "sqlite3": ("3.51.2", "Public Domain"),
                "utf8proc": ("2.11.3", "MIT")}
    for name, (version, license_name) in expected.items():
        entry = overrides.get(name, {})
        actual = entry.get("version-semver", entry.get("version"))
        if actual != version or entry.get("port-version") != 0:
            failures.append(f"{name} must be locked to {version}#0")
        if name not in {dependency.get("name") for dependency in manifest.get("dependencies", [])}:
            failures.append(f"{name} must be a manifest dependency")
        if version not in notice or license_name not in notice:
            failures.append(f"third_party/NOTICE.md is missing {name} version or license")

    if failures:
        print("Dependency validation failed:", file=sys.stderr)
        print("\n".join(f"- {failure}" for failure in failures), file=sys.stderr)
        return 1
    print("Validated vcpkg baseline, M1 dependency locks, and license notices.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
