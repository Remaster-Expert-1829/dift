# Implementation Report

- Task ID: `M0-001`
- Iteration number: 3
- Status: `BLOCKED`

## Requirement implemented

Iteration 3 repairs Independent Review iteration 5: exact tool-version parsing/equality validation with automated deceptive-version tests; CMake preset minimum 3.28.3; CI validation in every build/analysis environment; complete C++ formatting; enforceable clang-tidy naming rules and documented narrow suppressions; a controlled bad-project-header clang-tidy check; and artifact definition/reference validation with focused tests. M0 remains a C++17 build and quality foundation only: no transfer, protocol, storage, TLS, CDC, compression, scheduler, telemetry, or benchmark behavior was introduced.

## Files created, modified, or removed

- Created build/dependency files: `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `vcpkg-configuration.json`, and `cmake/bootstrap-vcpkg.sh`.
- Created support code: `include/spoink/support/*`, `src/support/*`, `src/cli/main.cpp`, and five `tests/unit/*_test.cpp` files.
- Created quality artifacts: `.clang-format`, `.clang-tidy`, `.github/workflows/ci.yml`, `scripts/check-dependencies.py`, `scripts/check-docs.py`, and `third_party/NOTICE.md`.
- Added validator tests: `tests/scripts/test_check_toolchain.py` and `tests/scripts/test_check_docs.py`, plus `scripts/verify-clang-tidy-header-gate.py`.
- Updated `.gitignore`; M0-specific additions to engineering, operations, traceability, consistency, and readiness documentation; replaced this stale report.
- Existing dirty documentation changes were preserved. Several modified documentation files predated this iteration.

## Behavioral changes

- `spoink version` prints `spoink 0.1.0`; other current invocations fail with usage and exit 2.
- Support provides structured Error/Result, checked unsigned 64-bit add/subtract/multiply and narrowing conversions, fixed 32-byte SHA-256 representation/hex decoding, strong IDs/byte units, deterministic fake monotonic time, and move-only POSIX descriptor/scoped cleanup.

## Database or migration changes

None. M0 creates no schema, persistent state, or wire format.

## Pinned versions and licenses

| Item | Pin | License |
|---|---|---|
| vcpkg | `2025.10.17`, `74e6536215718009aae747d86d84b78376bf9e09` | MIT |
| Catch2 | `3.11.0#0`, port tree `5f7385233a249ddbcc48f261b4e4c20f42297360` | BSL-1.0 |
| Linux toolchain baseline | Ubuntu 24.04; CMake 3.28.3; Ninja 1.11.1; GCC 13.3.0; Clang/clang-tidy 18.1.3 | BSD-3-Clause; Apache-2.0; GPL-3.0-or-later with GCC Runtime Library Exception; Apache-2.0 WITH LLVM-exception |

`vcpkg-configuration.json`, `vcpkg.json`, and `third_party/NOTICE.md` are the repository-controlled records. Direct inspection of the official vcpkg tag verified the Catch2 port/version/license. `TBD-ENG-DEP-001` is resolved for M0; future runtime dependencies were intentionally not added.

## Documentation changes

Documented artifact-ID definition/reference rules and inclusive ranges/wildcards in `docs/README.md`; clarified that a working newer local tool is not a reviewed toolchain lock in the engineering and operations documentation; and recorded exact tool-license enforcement in `third_party/NOTICE.md`.

## Architecture-plan deviations and reasons

None. Empty future libraries and placeholder APIs were intentionally omitted. TSan is deferred because M0 has no concurrency-focused code or tests, as required by the task and plan.

## Verification commands executed

| Command/check | Result |
|---|---|
| Official vcpkg `git ls-remote`, temporary clone, and Catch2 port/versions inspection | Passed; confirmed baseline, version, port-tree hash, and BSL-1.0. Temporary clone removed. |
| `scripts/check-dependencies.py` | Passed. |
| `python3 -m unittest discover --start-directory tests/scripts --pattern 'test_*.py'` | Passed: 9 tests. Includes exact-parser acceptance/rejection and valid/duplicate/dangling/malformed/file/anchor documentation cases. |
| `python3 scripts/check-docs.py` | Passed: links, anchors, status declarations, artifact-ID definitions, references, ranges, and wildcards. |
| `python3 scripts/check-toolchain.py` | Passed: exact Ubuntu tool versions only. |
| `git ls-files '*.cpp' '*.h' \| xargs clang-format-18 --dry-run --Werror` | Passed against 15 staged C++ files after clang-format-18 was applied to the full `include`, `src`, and `tests` inventory. |
| `cmake --preset clang-tidy && cmake --build --preset clang-tidy` | Passed after naming-policy repair. |
| `python3 scripts/verify-clang-tidy-header-gate.py` | Passed; a temporary lower-case type in a project header was rejected. |
| `cmake --preset dev`, `cmake --build --preset dev`, `ctest --preset dev`, install, version smoke | Passed: 10/10 tests; executable-only install; `spoink 0.1.0`. |
| `cmake --preset ci-gcc && cmake --build --preset ci-gcc && ctest --preset ci-gcc` | Passed: 10/10 tests with GCC 13.3.0. |
| JSON parsing of CMake/vcpkg configuration | Passed. |
| `git diff --check` | Passed. |
| Host probes for WSL, Docker/Podman, CMake, Ninja, GCC/Clang, and Python | No Linux distribution, container runtime, CMake/Ninja, or supported Linux compiler toolchain is installed. |

## Observed verification results

The supported Linux fresh-checkout commands are documented as:

```bash
bash cmake/bootstrap-vcpkg.sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
cmake --install out/build/dev --prefix "$PWD/out/install"
./out/install/bin/spoink version
```

Ubuntu 24.04 WSL verification completed: CMake 3.28.3, Ninja 1.11.1, GCC 13.3.0, Clang/clang-tidy/clang-format 18.1.3, pinned vcpkg, developer build/tests, GCC/Clang/release/sanitizer checks, install smoke, and the repaired clang-tidy build were run. The developer CTest suite passed all 10 tests.

## Acceptance-criteria status

| Criterion | Status |
|---|---|
| Fresh Linux configure/build/test/install | Passed on Ubuntu 24.04 WSL. |
| Presets; GCC/Clang warnings-as-errors; ASan/UBSan; release | Passed on Ubuntu 24.04 WSL: each compiler preset built and passed 10/10 tests; the documented ASan/UBSan environment passed 10/10 tests; release passed 10/10 tests and its install/version smoke printed `spoink 0.1.0`. |
| Focused M0 support tests | Passed: 10/10 tests. |
| Quality/scan/documentation CI | Validators, staged-source formatter, clang-tidy build/header negative test, dependency check, and docs check passed. Hosted gitleaks/dependency-review have not run because no M0 commit has been pushed. |
| Exact versions/licenses; `TBD-ENG-DEP-001` | Satisfied. |
| Independent review | Pending. |

## Known limitations

- Only `spoink version` exists in M0; no later CLI or transfer behavior is implied.
- Support headers are internal source contracts, not a stable installed ABI.
- TSan, fuzz, TLS, protocol, persistence, integration/E2E, and performance tests await their owning milestones.

## Remaining blockers

- M0 source/build/CI/scripts/tests/dependency/notice artifacts are staged, and `git ls-files` now enumerates all 15 C++ files. Relevant documentation has pre-existing uncommitted changes; it remains deliberately unstaged to preserve user work. Therefore no complete M0 Git commit or hosted CI run exists yet.

## Areas requiring focused review

Review the Result failure-call preconditions, POSIX wrapper placement, sanitizer flags, vcpkg bootstrap/provenance, and the absence of later-milestone behavior. Independently run the Linux commands before changing status to `READY_FOR_REVIEW`.
