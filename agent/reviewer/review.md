# Independent Review

- Task ID: M0-001
- Builder iteration reviewed: 2
- Review iteration: 5
- Verdict: FAIL

## Requirement coverage

1. Fresh Linux configure/build/test/install: PASS. The documented Ubuntu 24.04 sequence completed in an isolated copy. The developer build completed, all 10 tests passed, installation produced only bin/spoink, and the installed binary printed spoink 0.1.0.
2. Developer, CI, sanitizer, and release presets: PASS. dev, ci-gcc, ci-clang, sanitize, clang-tidy, and release all configured and built on Ubuntu 24.04. Every preset with tests passed 10/10 tests.
3. GCC/Clang C++17 warnings-as-errors: PASS. GCC 13.3.0 and Clang 18.1.3 builds used C++17 and the checked warning set with -Werror.
4. M0 support foundations and focused tests: PASS. PeerId is a strong 32-byte type, TransferId is a distinct strong 16-byte type, RequestId wraps uint64_t, and direct compile-time/runtime tests cover those contracts. Error/result, checked arithmetic, hash conversion, fake time, and owned-resource cleanup tests pass under both compilers and ASan/UBSan.
5. Quality and CI gates: FAIL. The clang-tidy preset now passes and its header filter covers absolute project-header paths, but global aNy_CasE settings disable three naming categories without the required suppression record. Documentation link and anchor checking works, but meaningful ID validation remains absent. The workflow formatting command examines zero files in this checkout because all 15 C++ files are untracked; checking those files directly reveals a clang-format 18 failure.
6. Exact versions, dependency lock, and licenses: FAIL. The vcpkg commit/baseline, Catch2 3.11.0#0 override, and third-party notices are consistent, and the project-license claim was removed. However, scripts/check-toolchain.py uses substring matching and accepts deliberately wrong versions, while CMakePresets.json declares 3.28.0 instead of the 3.28.3 project minimum.
7. Scope boundary: PASS. No M1+ transfer, persistence, protocol, networking, TLS, compression, scheduler, telemetry, benchmark, schema, or migration behavior was introduced.
8. Independent review: completed with FAIL.

The implementation repairs several Review iteration 4 findings, but acceptance criteria 5 and 6 remain unsatisfied.

## Repository evidence

- Read agent/task.md, agent/architecture/current-plan.md, agent/builder/implementation-report.md, docs/README.md, the approved engineering, quality, delivery, operations, workflow, milestone, Definition of Done, contract, architecture, data, and ADR documents, plus the live repository state.
- Inspected CMakeLists.txt, CMakePresets.json, cmake/bootstrap-vcpkg.sh, vcpkg.json, vcpkg-configuration.json, .clang-format, .clang-tidy, .github/workflows/ci.yml, all support/CLI source and headers, all Catch2 tests, all validation scripts, third_party/NOTICE.md, and relevant documentation changes.
- include/spoink/support/types.h lines 13-40 defines tag-parameterized identifiers, PeerId at 32 bytes, TransferId at 16 bytes, and a separate RequestId class containing uint64_t.
- tests/unit/types_test.cpp lines 22-40 directly asserts the approved sizes/value type, rejects a 16-byte PeerId construction, rejects PeerId-to-TransferId conversion, and checks runtime representations.
- CMakeLists.txt line 55 installs only the spoink runtime target. The clean install contained one file: bin/spoink.
- .clang-tidy line 13 uses an absolute-path-tolerant include/src header filter. The generated Ninja commands invoke clang-tidy-18 for every project translation unit with the checked-in configuration.
- .clang-tidy lines 17-32 set ClassCase, EnumConstantCase, and ConstantCase to aNy_CasE. docs/13_engineering/13_01_engineering_standards.md line 38 requires suppression owner, reason, scope, and removal condition and forbids blanket project-code suppressions.
- tests/unit/raii_test.cpp line 19 and include/spoink/support/result.h lines 23 and 44 contain narrow NOLINT comments with reasons, but no owner or removal condition.
- scripts/check-toolchain.py lines 7-11 records the expected versions but tests only whether the expected text is a substring of command output.
- CMakeLists.txt line 1 requires CMake 3.28.3; CMakePresets.json lines 3-7 declares cmakeMinimumRequired 3.28.0.
- scripts/check-docs.py builds local heading-anchor sets and validates missing files/anchors. Its only artifact-ID check is that each prefix literal appears in docs/README.md; it does not parse ID definitions/references or reject duplicates and dangling IDs.
- vcpkg.json contains only Catch2 and no project license field. vcpkg-configuration.json, bootstrap-vcpkg.sh, and scripts/check-dependencies.py use baseline 74e6536215718009aae747d86d84b78376bf9e09. third_party/NOTICE.md records vcpkg/MIT and Catch2 3.11.0#0/BSL-1.0.
- docs/16_operations/16_01_local_development_and_deployment.md line 9 correctly states that M0 pins Catch2 only and that OpenSSL, SQLite, LZ4, and Zstd are future dependencies.
- The implementation inventory contains only support utilities, a version-only CLI, tests, build/quality files, and documentation. Searches found no later-milestone implementation.
- Git status shows every M0 build, CI, source, test, script, and dependency file as untracked. git ls-files reports 0 tracked C++ headers/sources while the working tree contains 15.

## Independent verification

1. Confirmed Ubuntu 24.04.5 LTS x86-64 under WSL2. Captured CMake 3.28.3, Ninja 1.11.1, GCC 13.3.0, Clang/clang-tidy/clang-format 18.1.3, Python 3.12.3, and Git 2.43.0.
2. Created an isolated Linux copy excluding .git, .vcpkg, and out. The first bootstrap clone ended with a transient HTTP/2 early-EOF failure. After deleting only the confirmed incomplete 4 KB clone, reran the documented script with Git HTTP/1.1 transport: PASS. It checked out vcpkg baseline 74e6536215... and produced vcpkg tool 2025-10-16-71538f2694db93da4668782d094768ba74c45991.
3. Ran cmake --preset dev, cmake --build --preset dev, ctest --preset dev, installation to out/install, and the installed spoink version command: PASS. Build completed in 12 steps, 10/10 tests passed, and output was spoink 0.1.0.
4. Observed vcpkg resolve Catch2 3.11.0 from port tree 5f7385233a249ddbcc48f261b4e4c20f42297360 and report BSL-1.0/MIT licenses: PASS against the checked-in lock/notices.
5. Ran ci-gcc configure/build/CTest: PASS with GCC 13.3.0; 10/10 tests passed.
6. Ran ci-clang configure/build/CTest: PASS with Clang 18.1.3; 10/10 tests passed.
7. Ran the sanitize configure with the requested ASAN_OPTIONS and UBSAN_OPTIONS, then built and ran CTest: PASS; 10/10 tests passed with no sanitizer report.
8. Ran clang-tidy configure/build: PASS. Inspected ninja -t commands and confirmed clang-tidy-18 is invoked for project/test translation units using the checked-in .clang-tidy file. The header filter matches absolute include/spoink and src/spoink paths.
9. Ran release configure/build/CTest: PASS; 10/10 tests passed.
10. Ran python3 scripts/check-toolchain.py on the actual tools: PASS, printing “Validated exact M0 toolchain versions.”
11. Negative-tested scripts/check-toolchain.py by mocking outputs as CMake 3.28.30, Ninja 1.11.10, GCC 13.3.00, and LLVM tools 18.1.30: FAIL EFFECTIVENESS. The validator exited 0 and still printed “Validated exact M0 toolchain versions.”
12. Ran python3 scripts/check-dependencies.py: PASS.
13. Ran python3 scripts/check-docs.py: PASS over 39 Markdown documents.
14. Negative-tested the documentation validator with temporary fixtures. A missing local anchor correctly returned 1. Duplicate FR-999 entries plus an unbound FR-888 entry returned 0: link/anchor validation is repaired, but ID validation remains ineffective.
15. Inspected the clean install using find and file: PASS. It contained only bin/spoink, an x86-64 ELF executable; no internal headers or support library were installed.
16. Ran clang-format-18 --dry-run --Werror over every .h/.cpp under include, src, and tests: FAIL. include/spoink/support/error.h has the wrong standard-include order; clang-format moves cstdint before string.
17. Ran the workflow's exact git ls-files formatting pipeline against the live checkout: it exited successfully but processed no files. git ls-files '*.cpp' '*.h' returned 0; direct inventory found 15 files.
18. Ran git diff --check from Ubuntu against the live checkout: PASS with no whitespace errors.
19. Inspected git status, diff/stat, changed documentation, workflow, dependency lock, tests, public/install interfaces, error paths, and M1+ keyword/inventory scans. No M1+ behavior, schema, migration, runtime dependency, or compatibility surface was added.
20. Removed the confirmed isolated /var/tmp review copy after testing; 722 MB of generated build/dependency artifacts were deleted. Production files were not modified.

Not independently executed: GitHub-hosted gitleaks and dependency-review Actions. No remote CI run representing this implementation exists because the workflow and implementation remain untracked.

## Findings

### Finding 1 — Exact toolchain validation is not exact

- Severity: High
- Exact file, component, or behavior: scripts/check-toolchain.py lines 7-11; CMakePresets.json lines 3-7; CMakeLists.txt line 1; .github/workflows/ci.yml tool installation/validation jobs.
- Evidence: The script uses “if version not in output”. Mocked wrong superstring versions 3.28.30, 1.11.10, 13.3.00, and 18.1.30 all passed. Preset metadata says 3.28.0 while the project requires 3.28.3. CI installs mutable apt package families; only a separate repository-check job calls the ineffective validator.
- User or system impact: Tool drift can be reported as an exact match, undermining the reproducible-toolchain acceptance criterion and allowing build jobs to run a different patch level than reviewed.
- Required correction: Parse each tool's authoritative version token and compare it for equality; align preset metadata to 3.28.3; apply validation to the build/analysis jobs or use a reviewed immutable tool image/snapshot.
- Required verification after correction: Add automated positive and negative tests for every tool parser, including superstring versions, and rerun the full Ubuntu matrix.
- Classification: Defect introduced by the current implementation.

### Finding 2 — Formatting gate is vacuous for this submission and the actual sources fail format

- Severity: High
- Exact file, component, or behavior: .github/workflows/ci.yml formatting step; include/spoink/support/error.h lines 3-4; live Git index.
- Evidence: All 15 C++ files and the workflow are untracked. The workflow enumerates git ls-files, which returns zero and exits successfully. Direct clang-format 18 verification over the working tree fails because error.h places string before cstdint.
- User or system impact: The required formatting gate gives a false green result, and no submitted revision or remote CI run contains the reviewed implementation.
- Required correction: Apply clang-format 18 to the failing header, include the entire M0 implementation/workflow in the submitted Git revision through the normal workflow, and run CI on that revision.
- Required verification after correction: Confirm git ls-files enumerates all intended source/header files, rerun the exact workflow formatting command, inspect the submitted diff, and review all CI job results.
- Classification: Current implementation and submission-state defect.

### Finding 3 — Documentation ID validation remains ineffective

- Severity: Medium
- Exact file, component, or behavior: scripts/check-docs.py lines 15 and 44-46; documentation CI step.
- Evidence: Missing local files and anchors are now detected, but a fixture with duplicate FR-999 and dangling FR-888 entries returned success because only legend prefix literals are checked.
- User or system impact: CI can claim link/ID validation while duplicate, malformed, or dangling traceability IDs remain undetected.
- Required correction: Define and parse meaningful artifact-ID definitions/references, including documented wildcard/range rules, and reject duplicates, malformed IDs, and dangling references.
- Required verification after correction: Add self-tests covering valid documents plus duplicate ID, dangling ID, malformed ID, missing file, and missing anchor failures.
- Classification: Incomplete repair of a current quality-script defect.

### Finding 4 — clang-tidy passes by using unjustified broad naming suppressions

- Severity: Medium
- Exact file, component, or behavior: .clang-tidy lines 17-32; tests/unit/raii_test.cpp line 19; include/spoink/support/result.h lines 23 and 44; engineering suppression policy.
- Evidence: Header coverage and the preset are repaired, but ClassCase, EnumConstantCase, and ConstantCase are globally set to aNy_CasE. No owner, scope, or removal condition is recorded. Narrow NOLINT comments state a reason and line scope but still omit the policy's owner/removal fields.
- User or system impact: Naming diagnostics are disabled project-wide, so the static-analysis gate can pass without enforcing the checked-in convention; suppression debt has no accountable lifecycle.
- Required correction: Configure enforceable naming cases/prefixes for the chosen convention and remove the global aNy_CasE exemptions. For any genuinely necessary NOLINT, record owner, reason, scope, and removal condition in the repository-approved form.
- Required verification after correction: Rerun the checked-in clang-tidy preset and demonstrate that a temporary violating project-header fixture is rejected while reviewed narrow suppressions pass.
- Classification: Current static-analysis policy defect.

### Repaired Review iteration 4 findings

- ID representations and direct contract tests: repaired and independently passed.
- Executable-only installation: repaired and independently passed.
- Project-license claim: removed; third-party notices remain aligned.
- Operations wording for future OpenSSL/SQLite/LZ4/Zstd: repaired.
- clang-tidy execution and project-header filter: repaired; suppression policy remains Finding 4.
- Documentation local link/anchor detection: repaired; artifact-ID semantics remain Finding 3.
- Linux verification blocker: removed; all requested build/test presets now have independent Ubuntu evidence.

### Relevant pre-existing defects

None identified that block this iteration beyond repository state already reported in Finding 2.

### Optional improvements that do not block approval

- Pin third-party GitHub Actions to reviewed commit SHAs rather than mutable major-version tags.
- Make check-dependencies.py verify that the bootstrap commit, registry baseline, manifest override, port tree, and notices all agree rather than checking only selected literals.
- Constrain ScopedCleanup to nothrow-invocable cleanup/move semantics and document Result.error() preconditions.

## Builder repair instructions

1. Replace substring tool-version checks with exact parsers/comparisons; align CMakePresets.json to CMake 3.28.3; add negative parser tests and enforce the check in each relevant CI environment.
2. Run clang-format 18 on all M0 C++ files and fix error.h include order.
3. Replace global aNy_CasE clang-tidy settings with enforceable naming configuration; bring every NOLINT/suppression record into compliance with owner, reason, scope, and removal-condition policy.
4. Implement actual documentation artifact-ID validation and validator self-tests while retaining the repaired local link/anchor checks.
5. Include every M0 build, CI, source, test, script, dependency, notice, and relevant documentation file in the submitted Git revision through the normal workflow.
6. Run the documented Ubuntu 24.04 sequence and every preset/check again, then run and review the complete GitHub CI workflow including formatting, gitleaks, and dependency review where applicable.
7. Submit Builder iteration 3 with exact command outputs and identify the reviewed revision.

## Human decision required

Not required.

## Remaining risks

- The actual Ubuntu build, compiler matrix, tests, sanitizers, release build, tidy execution, install smoke, dependency lock, and runtime scope all have independent passing evidence.
- Current positive validator outputs overstate enforcement: toolchain “exact” validation and artifact-ID validation both pass cases they should reject.
- Remote secret/dependency scanning and GitHub runner behavior remain unobserved.
- The implementation has no wire/storage compatibility risk yet because those surfaces are correctly deferred.
- vcpkg internally downloaded CMake 3.30.1 to build its port even though the project itself ran under CMake 3.28.3. This is dependency-manager tooling, not evidence that the project minimum failed, but it should be understood when claiming a completely closed tool set.

## Final assessment

Builder iteration 2 substantially repairs the prior implementation and independently builds cleanly on Ubuntu 24.04 with GCC, Clang, ASan/UBSan, release, and clang-tidy configurations. Its type contracts, package contents, vcpkg/Catch2 lock, notices, operations wording, and M0-only scope are correct.

The repository still does not satisfy agent/task.md. Exact tool versions are not actually enforced, documentation IDs are not validated, clang-tidy relies on policy-noncompliant global naming suppressions, the true C++ inventory fails clang-format 18, and the complete implementation remains outside Git and therefore outside observed CI. The required result for Builder iteration 2 is FAIL.
