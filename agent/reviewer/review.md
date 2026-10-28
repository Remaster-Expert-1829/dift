# Independent Review

- Task ID: M0-001
- Builder iteration reviewed: 6
- Review iteration: 9
- Reviewed remote main: 863bebeb8c69ccd08139994f870f0c8e6f2eb493
- Hosted PR head: 44e2b14fabfcf577388b38c91b8ef61ef175b6bf (tree-identical to reviewed remote main)
- Verdict: APPROVED

## Requirement coverage

1. Fresh Linux configure/build/test/install: PASS. Build, dependency, source, and test inputs are unchanged from the complete Review iterations 7 and 8 Linux verification. Final PR-head run 40738534004 independently bootstrapped, configured, built, passed tests, installed the release executable, and printed spoink 0.1.0.
2. Developer, CI, sanitizer, analysis, and release presets: PASS. Final PR-head run 40738534004 completed GCC, Clang, release/install smoke, clang-tidy/header gate, and ASan/UBSan jobs successfully.
3. GCC/Clang C++17 warnings-as-errors: PASS. Hosted logs show GNU 13.3.0 and Clang 18.1.3 selected, with 10/10 tests passing in both compiler jobs. The checked CMake configuration still requires C++17 and applies the reviewed warning set plus -Werror.
4. M0 support foundations: PASS. PeerId remains a distinct tagged 32-byte identifier, TransferId a distinct tagged 16-byte identifier, and RequestId a distinct uint64_t wrapper. Direct compile-time and runtime contract tests remain present and passed.
5. Quality and hosted CI gates: PASS. Repository checks, formatting, validator unit tests, documentation validation, dependency review, secret scan, clang-tidy/header gate, ASan/UBSan, compiler builds/tests, and release install smoke all completed successfully in the hosted PR run.
6. Exact tools, dependency lock, and notices: PASS. Every relevant hosted job asserted /usr/bin/cmake and /usr/bin/ninja before exact validation and substantive work. The approved CMake 3.28.3, Ninja 1.11.1, GCC 13.3.0, LLVM 18.1.3, vcpkg baseline 74e6536215718009aae747d86d84b78376bf9e09, Catch2 3.11.0#0, and license records are unchanged.
7. Documentation validation and alignment: PASS. The focused validator tests remain 9/9, check-docs passes, and the documentation continues to treat OpenSSL, SQLite, LZ4, and Zstd as future dependencies.
8. Reviewer iteration 7 path-selection repair: PASS. .github/workflows/ci.yml installs tools, writes /usr/bin to GITHUB_PATH in a separate step, then asserts command -v cmake and command -v ninja before validation and substantive work.
9. Dependency-review permission scope: PASS. The workflow-level permissions contain only contents: read, no workflow or job grants pull-requests: write, and raw logs for all eight jobs in run 40737223533 show only Contents: read and Metadata: read. Basic dependency-review and every other job passed with that read-only token.
10. M0 scope boundary: PASS. Source inventory and targeted searches found no transfer engine, persistence, protocol, networking, TLS, compression, scheduler, telemetry, benchmark, schema, or migration behavior.

## Repository evidence

- Fetched remote main and confirmed it resolves to 863bebeb8c69ccd08139994f870f0c8e6f2eb493.
- 863beb is a merge of the least-privilege repair branch. Its tree is identical to final hosted PR head 44e2b14fabfcf577388b38c91b8ef61ef175b6bf, so run 40738534004 tested the reviewed tree.
- The delta from Review iteration 8 changes only .github/workflows/ci.yml and agent/builder/implementation-report.md. The workflow change removes one line: the workflow-wide pull-requests: write grant. Build files, presets, source, tests, validation scripts, dependency locks, notices, and product documentation did not change.
- Read agent/task.md, agent/architecture/current-plan.md, the Builder iteration 6 report at the reviewed revision, the previous reviewer report, approved documentation, and the live repository state.
- Inspected the workflow, CMake/presets, vcpkg/bootstrap, support/CLI source, headers, Catch2 tests, Python validators/tests, notices, and relevant documentation.
- include/spoink/support/types.h defines Identifier<Tag, ByteCount>, PeerId as Identifier<PeerIdTag, 32>, TransferId as Identifier<TransferIdTag, 16>, and a distinct RequestId class with a uint64_t value.
- tests/unit/types_test.cpp directly asserts PeerId::kByteCount == 32, TransferId::kByteCount == 16, RequestId.value() has uint64_t type, 16 bytes cannot construct PeerId, and PeerId is not convertible to TransferId.
- CMakeLists.txt installs only the spoink runtime target. The new isolated install inventory contained only bin/spoink.
- CMakePresets.json records cmakeMinimumRequired 3.28.3 and separate GCC, Clang, sanitizer, tidy, developer, and release presets.
- scripts/check-toolchain.py has anchored vendor-specific parsers and compares parsed values with exact expected strings. tests/scripts/test_check_toolchain.py explicitly rejects 3.28.30, 1.11.10, 13.3.00, and 18.1.30.
- .github/workflows/ci.yml separately installs packages, appends /usr/bin through GITHUB_PATH, and asserts /usr/bin/cmake and /usr/bin/ninja in repository-checks, the three-entry build matrix, clang-tidy, and ASan/UBSan.
- .github/workflows/ci.yml lines 8-9 contain the complete workflow-level permission map: contents: read. No workflow or job permission grants pull-requests: write.
- GitHub documents that workflow-level permissions apply to all jobs and that jobs.<job_id>.permissions is the mechanism for minimum per-job access: https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#jobsjob_idpermissions
- The dependency-review action's basic configuration requires contents: read. Its documentation says pull-requests: write is additionally needed when PR-comment summaries are enabled; this workflow does not configure comment-summary-in-pr: https://github.com/actions/dependency-review-action/blob/main/docs/examples.md
- .clang-tidy uses CamelCase/lower_case rules with k prefixes for constants and enum constants; no aNy_CasE setting exists. Its header filter matches absolute include/spoink project-header paths.
- The three NOLINT sites in result.h and raii_test.cpp include owner, reason, scope, and removal condition. No blanket directory exclusion exists.
- scripts/check-docs.py parses documented definitions/references, ranges, wildcards, local files, and anchors. tests/scripts/test_check_docs.py covers valid range/wildcard input and duplicate, dangling, malformed, missing-file, and missing-anchor failures.
- vcpkg.json contains only Catch2 and no license field for Spoink. vcpkg-configuration.json and bootstrap-vcpkg.sh use the same baseline. third_party/NOTICE.md records vcpkg/MIT and Catch2 3.11.0#0/BSL-1.0.
- docs/16_operations/16_01_local_development_and_deployment.md states that OpenSSL, SQLite, LZ4, and Zstd are future dependencies not acquired or linked by M0.
- Source inventory and keyword inspection found only support utilities and the version-only CLI; no later-milestone implementation, schema, or persistent/public compatibility surface exists.

## Independent verification

### Retained Review iteration 7 baseline

1. Ran git status, git rev-parse HEAD, recent log, commit stats, tree inventory, and diffs. Result: clean initial checkout at 3fda33572521d0419ec4671a0a8daa1a994cb092; b75cc9e contains the substantive M0 implementation.
2. Created an isolated local Ubuntu clone from the live repository. Result: clean committed checkout at the exact reviewed HEAD; no uncommitted host files entered verification.
3. Captured the platform and tools: Ubuntu 24.04.5 LTS x86-64; CMake 3.28.3; Ninja 1.11.1; GCC 13.3.0; Clang, clang-tidy, and clang-format 18.1.3.
4. Ran bash cmake/bootstrap-vcpkg.sh exactly. Result: PASS; checked out vcpkg 74e6536215... and bootstrapped tool version 2025-10-16-71538f2694db93da4668782d094768ba74c45991.
5. Ran the dev configure/build/CTest/install/version sequence. Result: PASS; 12 build steps, 10/10 tests, only bin/spoink installed, output spoink 0.1.0.
6. Ran ci-gcc configure/build/CTest. Result: PASS with GCC 13.3.0 and 10/10 tests.
7. Ran ci-clang configure/build/CTest. Result: PASS with Clang 18.1.3 and 10/10 tests.
8. Ran sanitize configure with the requested ASAN_OPTIONS and UBSAN_OPTIONS, followed by build and CTest. Result: PASS; 10/10 tests and no sanitizer report.
9. Ran clang-tidy configure/build. Result: PASS; all 12 build steps completed under clang-tidy 18.
10. Ran python3 scripts/verify-clang-tidy-header-gate.py. Result: PASS; the controlled project-header naming violation was rejected.
11. Independently created temporary source/header fixtures in the isolated clone where a normal translation unit included a violating include/spoink header. Result: clang-tidy returned failure with readability-identifier-naming for the header, proving HeaderFilterRegex applies to included project headers. Fixtures were automatically removed.
12. Ran release configure/build/CTest/install/version. Result: PASS; 10/10 tests, only bin/spoink installed, output spoink 0.1.0.
13. Ran python3 -m unittest discover --start-directory tests/scripts --pattern test_*.py. Result: PASS; 9/9 tests.
14. Inspected and exercised the exact-version tests. Result: exact versions accepted; deceptive CMake 3.28.30, Ninja 1.11.10, GCC 13.3.00, and LLVM 18.1.30 values rejected, as was an unrecognized vendor format.
15. Ran python3 scripts/check-toolchain.py. Result: PASS, “Validated exact M0 toolchain versions.”
16. Ran python3 scripts/check-dependencies.py. Result: PASS, baseline/Catch2 lock/notices validated.
17. Ran python3 scripts/check-docs.py. Result: PASS for the committed documentation corpus.
18. Independently exercised extra temporary documentation fixtures. Result: a range with a missing member produced dangling FR-002; a descending range failed; wildcard definitions failed; malformed double-wildcard syntax failed. Fixtures were automatically removed.
19. Ran git ls-files for C++ files and the requested clang-format-18 dry run. Result: all 15 tracked .h/.cpp files were enumerated and formatting passed.
20. Ran git diff --check HEAD^. Result: PASS. Also ran git diff --check 4a4f941..HEAD over the complete substantive M0 range: PASS.
21. Inspected the vcpkg checkout. Result: baseline is tagged 2025.10.17; Catch2 port tree 5f7385233a249ddbcc48f261b4e4c20f42297360 declares version 3.11.0 and license BSL-1.0 and pins its upstream source with SHA-512.
22. Observed vcpkg installation output. Result: Catch2 3.11.0 plus the two vcpkg host helpers were restored; declared package licenses were BSL-1.0 and MIT. No future runtime dependency was installed by the manifest.
23. Scanned implementation/build surfaces for M1+ behavior and inspected public/install interfaces, errors, dependency boundaries, and documentation alignment. Result: no M1+ behavior or schema/migration exists.
24. Confirmed the isolated clone remained clean apart from ignored generated output, then removed the confirmed 723 MB /var/tmp clone. Production files were not modified.
25. Queried the GitHub Actions API for reviewed HEAD 3fda33572521d0419ec4671a0a8daa1a994cb092. Result: push run 40679859232 completed with failure. secrets passed; dependency-review was skipped as designed for a push rather than a pull request.
26. Inspected every failed job and its authenticated log. Result: repository-checks, ci-gcc, ci-clang, release, clang-tidy, and ASan/UBSan all failed at their exact-toolchain validation step with the same messages: cmake expected 3.28.3 but got 3.31.6; ninja expected 1.11.1 but got 1.13.2. No affected job reached bootstrap, configure, build, test, install, clang-tidy, sanitizer, validator-unit-test, documentation, or formatting steps.
27. Inspected the hosted installation log. Result: apt successfully installed the approved cmake 3.28.3-1build7 and ninja-build 1.11.1-2 packages, proving this is workflow PATH selection rather than unavailable Ubuntu packages. The exact validator behaved correctly by rejecting the selected runner-toolcache versions.

Those results establish the independently verified baseline before the workflow-only repair. The old hosted run was red and dependency-review was then pending; the following checks supersede that hosted status for Review iteration 8.

### Review iteration 8 verification

1. Ran git status, git rev-parse, git log, git fetch origin main, git show, tree comparisons, and targeted diffs. Result: remote main is exact requested revision bf31702; the PR head and merge revision have identical trees; only agent/reviewer/review.md is modified in the live worktree.
2. Inspected agent/task.md, agent/architecture/current-plan.md, the Builder iteration 5 report at the reviewed revision, the previous reviewer report, the workflow, CMake/presets, support types/tests, validation scripts, dependency manifests, notices, operations documentation, and source inventory.
3. Queried [GitHub Actions run 40690524048](https://github.com/Remaster-Expert-1829/dift/actions/runs/40690524048) and every job through the GitHub API. Result: pull_request event, completed success; dependency-review, repository-checks, ci-gcc, ci-clang, release, clang-tidy, ASan/UBSan, and secrets all succeeded with no failed steps.
4. Inspected the authenticated hosted logs. Result: relevant jobs selected /usr/bin/cmake and /usr/bin/ninja and printed "Validated exact M0 toolchain versions."
5. Inspected compiler/test logs. Result: ci-gcc used GNU 13.3.0 and passed 10/10; ci-clang used Clang 18.1.3 and passed 10/10; release used GNU 13.3.0, passed 10/10, installed successfully, and printed spoink 0.1.0; ASan/UBSan passed 10/10; clang-tidy and the project-header gate steps completed successfully.
6. Inspected dependency and secret logs. Result: dependency review reported no vulnerable packages at low severity or higher and no denied packages; gitleaks reported no leaks.
7. Inspected GITHUB_TOKEN permission blocks in dependency-review, repository-checks, secrets, release, clang-tidy, and ASan/UBSan logs. Result: every inspected job received Contents: read and PullRequests: write, confirming the write permission is workflow-wide.
8. Compared the checked workflow against GitHub's official workflow-permission and dependency-review-action documentation. Result: job-level permissions are available for least privilege; the checked basic dependency-review invocation does not enable the PR-comment feature that documents pull-requests: write as necessary.
9. Created an isolated checkout at exact bf31702 on Ubuntu 24.04.5 x86-64. Verified existing review tools: CMake 3.28.3, Ninja 1.11.1, GCC 13.3.0, Clang/clang-tidy/clang-format 18.1.3, and pinned vcpkg commit 74e6536215718009aae747d86d84b78376bf9e09.
10. Ran cmake --preset dev, cmake --build --preset dev, ctest --preset dev, installation, and the installed version smoke. Result: PASS; 12 build steps, 10/10 tests, only bin/spoink installed, output spoink 0.1.0.
11. Ran python3 -m unittest discover --start-directory tests/scripts --pattern 'test_*.py'. Result: PASS, 9/9.
12. Ran scripts/check-toolchain.py, scripts/check-dependencies.py, and scripts/check-docs.py. Result: all PASS.
13. Ran the tracked .h/.cpp clang-format-18 dry run and git diff --check across the reviewed delta. Result: PASS.
14. Inspected types.h, types_test.cpp, the install rule, all source/test paths, and targeted M1+ keywords. Result: approved ID contracts and executable-only package remain; no M1+ behavior was found.
15. A redundant fresh vcpkg network clone stalled in git-remote-https at 3.3 MB and was cancelled. Focused compilation used the previously independently verified checkout at the exact locked commit; the current hosted jobs independently bootstrapped that same pin successfully. The verified 54 MB temporary checkout was removed afterward.

### Review iteration 9 verification

1. Ran git status, git rev-parse, git log, git fetch origin main, git show, tree comparisons, and targeted diffs. Result: remote main is exact requested revision 863bebeb8c69ccd08139994f870f0c8e6f2eb493; its tree equals final PR head 44e2b14fabfcf577388b38c91b8ef61ef175b6bf; only agent/reviewer/review.md is modified in the live worktree.
2. Inspected agent/task.md, agent/architecture/current-plan.md, the Builder iteration 6 report at the reviewed revision, the previous reviewer report, .github/workflows/ci.yml, and the unchanged M0 implementation/dependency/documentation surfaces.
3. Compared bf31702..863beb. Result: the only substantive change is removal of workflow-level pull-requests: write; the Builder report is the only other changed file. git diff --check passed.
4. Queried [PR run 40737223533](https://github.com/Remaster-Expert-1829/dift/actions/runs/40737223533) and downloaded every authenticated job log. Result: all eight jobs succeeded; every effective token block contained exactly Contents: read and Metadata: read; no PullRequests scope appeared.
5. Queried [final PR-head run 40738534004](https://github.com/Remaster-Expert-1829/dift/actions/runs/40738534004) and every job/step. Result: dependency-review, repository-checks, ci-gcc, ci-clang, release, clang-tidy, ASan/UBSan, and secrets all succeeded with no failed steps.
6. Inspected the final run logs. Result: relevant jobs selected /usr/bin/cmake and /usr/bin/ninja and passed exact tool validation; GCC 13.3.0 and Clang 18.1.3 each passed 10/10 tests; release passed 10/10, installed, and printed spoink 0.1.0; ASan/UBSan passed 10/10; clang-tidy and its header gate completed; validator tests ran 9/9; dependency/docs/format checks passed; dependency-review found no vulnerable or denied packages; gitleaks found no leaks.
7. Reinspected the approved ID definitions/tests, executable-only install rule, CMake/vcpkg pins, notices, and implementation inventory at 863beb. Result: all earlier M0 contracts remain unchanged and correct.
8. Searched target implementation surfaces for networking, TLS, persistence, compression, scheduler, telemetry, benchmark, schema, and migration markers. Result: no M1+ implementation was found.

## Findings

### Defects introduced by the current implementation

None identified.

### Repaired earlier findings

- Exact toolchain enforcement: repaired and independently positive/negative tested.
- Formatting and committed source inventory: repaired; all 15 tracked files pass clang-format 18.
- Documentation artifact-ID validation: repaired and independently adversarially tested.
- clang-tidy naming policy/suppression records: repaired; full tidy build and header-negative probes pass.
- Git submission state: repaired; remote main is reviewed merge revision 863beb and matches final hosted PR head 44e2b14.
- Hosted tool selection: repaired; all relevant jobs selected and validated /usr/bin/cmake and /usr/bin/ninja before substantive work.
- GitHub token authorization: repaired; no job receives pull-request write access, and basic dependency-review passes with read-only permissions.

### Relevant pre-existing defects

None identified.

### Optional improvements that do not independently block approval

- Pin third-party GitHub Actions to reviewed commit SHAs instead of mutable major-version tags.
- Upgrade actions/checkout from v4 to a reviewed Node.js 24-capable release; the hosted run warns that checkout v4 targets deprecated Node.js 20 and is currently being forced onto Node.js 24.
- Remove CMAKE_C_COMPILER from CXX-only presets or enable C explicitly to avoid the harmless “manually-specified variable was not used” configure warning.
- The committed header-gate script invokes a header as the primary clang-tidy input. The independent include-based probe passed, so coverage is established; extending the script to use that stronger include-based shape would preserve the proof in CI.

## Builder repair instructions

Not required.

## Human decision required

Not required.

## Remaining risks

- Review iteration 9 did not repeat the entire local fresh-checkout matrix because build, dependency, source, test, and documentation inputs did not change. Earlier independent Linux verification and the complete final hosted run cover those unchanged surfaces.
- GitHub Actions use mutable major-version tags, creating a non-blocking supply-chain reproducibility risk.
- vcpkg fetched registry metadata and an internal CMake 3.30.1 tool while resolving the pinned port. The project itself configured with the reviewed CMake 3.28.3, and the resolved Catch2 port tree was pinned and verified; this is dependency-manager tooling rather than project toolchain drift.
- M1+ persistence, protocol, authorization, transfer, security, recovery, and compatibility behavior is intentionally absent and therefore outside this M0 validation.

## Final assessment

APPROVED. Remote main 863bebeb8c69ccd08139994f870f0c8e6f2eb493 satisfies agent/task.md for M0-001. The iteration-8 authorization defect is repaired: the workflow declares only contents: read, no job grants pull-requests: write, every effective token block is read-only, and dependency-review remains functional.

The final tree preserves the approved strong ID contracts, executable-only installation, exact tool/dependency locks, tests, analysis gates, documentation validation, and accurate license notices. The final PR-head run completes the full required CI matrix, and no M1+ behavior was introduced.
