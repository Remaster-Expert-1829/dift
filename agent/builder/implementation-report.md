# Implementation Report

- Task ID: `M0-001`
- Iteration number: 4
- Status: `READY_FOR_REVIEW`

## Requirement implemented

Iteration 4 repairs Independent Review iteration 7: every affected GitHub Actions job now adds `/usr/bin` through `GITHUB_PATH` in one step, then separately asserts `command -v cmake` and `command -v ninja` before exact toolchain validation and all substantive work. This selects CMake 3.28.3 and Ninja 1.11.1 instead of the runner toolcache. M0 remains a C++17 build and quality foundation only: no transfer, protocol, storage, TLS, CDC, compression, scheduler, telemetry, or benchmark behavior was introduced.

## Files created, modified, or removed

- Created build/dependency files: `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `vcpkg-configuration.json`, and `cmake/bootstrap-vcpkg.sh`.
- Created support code: `include/spoink/support/*`, `src/support/*`, `src/cli/main.cpp`, and five `tests/unit/*_test.cpp` files.
- Created quality artifacts: `.clang-format`, `.clang-tidy`, `.github/workflows/ci.yml`, `scripts/check-dependencies.py`, `scripts/check-docs.py`, and `third_party/NOTICE.md`.
- Modified `.github/workflows/ci.yml` in repair commit `8146ad23d1338d6f5f09ae89b35ce97945393a99` to select and assert `/usr/bin/cmake` and `/usr/bin/ninja` before the exact-version gate.
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
| GitHub Actions push run `40687177678` | Passed: repository-checks, ci-gcc, ci-clang, release, clang-tidy/header gate, ASan/UBSan, and secrets. https://github.com/Remaster-Expert-1829/dift/actions/runs/40687177678 |
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
| Quality/scan/documentation CI | Hosted run 40687177678 passed repository checks (dependency, exact tools, validator tests, docs, formatting), GCC/Clang/release builds/tests, clang-tidy/header gate, ASan/UBSan, and gitleaks. Dependency-review awaits its required pull-request run. |
| Exact versions/licenses; `TBD-ENG-DEP-001` | Satisfied. |
| Independent review | Pending. |

## Known limitations

- Only `spoink version` exists in M0; no later CLI or transfer behavior is implied.
- Support headers are internal source contracts, not a stable installed ABI.
- TSan, fuzz, TLS, protocol, persistence, integration/E2E, and performance tests await their owning milestones.

## Remaining blockers

None for the CI-path repair. The required pull-request dependency-review run is the final remote workflow evidence before re-review.

## Areas requiring focused review

Review the Result failure-call preconditions, POSIX wrapper placement, sanitizer flags, vcpkg bootstrap/provenance, and the absence of later-milestone behavior. Independently run the Linux commands before changing status to `READY_FOR_REVIEW`.
