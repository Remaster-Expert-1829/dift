# Architecture Plan

- Task ID: M0-001
- Status: READY_FOR_BUILD

## Task interpretation and architectural assessment

M0-001 establishes Spoink's C++17 Linux build, dependency, quality, and support-code foundation. It implements no file-transfer behavior. Architectural planning is warranted because the first target dependencies, cross-module support contracts, and pinned dependency mechanism will constrain later milestones. Approved documents already settle the relevant boundaries; M0 needs no new ADR or product-owner decision.

## Verified current-state findings

- agent/task.md defines M0-001, its scope, non-goals, acceptance criteria, and Architect/Builder/Reviewer handoff.
- docs/README.md names the approved corpus as the implementation source of truth. docs/25_implementation_readiness_review.md explicitly lists no M0 blocker and assigns TBD-ENG-DEP-001 to M0.
- docs/19_planning/19_01_milestones.md requires CMake targets/presets, pinned dependencies, support utilities, quality CI, documentation checks, and a fresh-checkout Linux configure/build/test/install.
- docs/13_engineering/13_01_engineering_standards.md requires C++17, Linux GCC/Clang, warnings-as-errors for project code, inward module dependencies, Result/Error, checked lengths, strong types, RAII, and fake time. It prefers pinned vcpkg manifest mode with find_package consumers.
- docs/04_architecture/04_01_system_overview.md and 04_02_components_and_runtime_flows.md assign bounded buffers, errors, configuration, and utilities to support. Their physical module layout is guidance; ownership and dependency direction are normative. docs/06_contracts/06_03_internal_component_contracts.md defines the shared result/error, byte-view, size, clock, and ownership conventions; suggested C++ names are not a required ABI.
- Accepted ADR-001 through ADR-007 already choose direct paired peers, stable logical chunks, TLS identity, hybrid SQLite/filesystem storage, bounded control/data connections, configurable compression, and versioned CBOR framing. None is implemented in M0.
- docs/14_quality/14_01_test_strategy.md, docs/15_delivery/15_01_ci_release_and_rollback.md, docs/16_operations/16_01_local_development_and_deployment.md, and docs/21_definition_of_done.md define the relevant test, CI, installation, and documentation gates. docs/06_contracts/06_02_cli_and_configuration.md reserves the spoink version command.
- Repository inspection found documentation, an untracked .gitignore, and tracked agent handoffs, but no CMake configuration, dependency manifest, CI workflow, source, public headers, or tests. Documentation has existing uncommitted edits. The .gitignore pattern names agent, but already tracked handoff files remain tracked. The older Builder and Reviewer reports describe the previous missing-task checkout and are not M0 evidence.

## Proposed architecture and resolved engineering choices

1. Use CMake as the only build definition and Ninja on the documented Linux x86-64 baseline. Use vcpkg manifest mode with a repository-pinned vcpkg commit and baseline. CMake consumes package targets through find_package; business code has no package-manager paths. This resolves the mechanism for TBD-ENG-DEP-001. The Builder selects and records exact working GCC, Clang, CMake, Ninja, vcpkg, dependency/port, test-framework, and analysis/scanning versions and licenses from the actual Linux CI image. If a required vcpkg port, license, or build mode is unsuitable, the engineering standard already permits a documented alternative pinned mechanism with evidence. Do not use unpinned FetchContent or link unused future runtime libraries merely to claim integration.
2. Create a reusable spoink_support target, a unit-test target, and an installable spoink executable with the already documented version command for package smoke. Developer, CI, ASan+UBSan, and release configure/build/test presets share the same CMake definitions. GCC and Clang use separate CI build trees. Apply warning failures to project targets, with third-party headers marked external/system. Preserve runtime validation in release builds.
3. Keep domain-neutral types and policies in include/spoink/support/ and src/support/, with focused tests in tests/unit/. These files have no dependency on CLI, sockets, TLS, SQLite, codecs, filesystem state, or transfer state. Create later module/test directories only as useful skeletons; do not add placeholder public APIs or empty libraries just to fill a diagram. Adapters will depend inward.
4. Implement Result<T, Error>, including void, with explicit success/failure handling. Error carries stable code/category, retry class, safe message key, correlation ID, scope/resumability, and a local-only diagnostic as specified by the internal contracts and failure policy. Avoid choosing future wire error codes now. Provide checked unsigned 64-bit add/subtract/multiply and checked conversions to size_t and signed/library integer types before allocation or offset operations. Use distinct strong types for peer/transfer/request IDs, hashes, offsets, and byte lengths as needed; SHA-256 values hold exactly 32 bytes with explicit representation conversion. Digest computation and random ID generation belong to later cryptographic/identity modules. Use an injectable monotonic clock and deterministic fake clock; keep UTC wall time separate. Provide narrow move-only RAII wrappers for resources actually exercised in M0, such as a POSIX descriptor and scoped cleanup. Add TLS/SQLite-specific wrappers with their adapters later. Destructors must not throw.
5. Run checked-in format verification, clang-tidy, GCC/Clang warnings-as-errors, unit tests, ASan+UBSan, dependency/lock/license/security and secret scans, relative-link/ID documentation checks, and clean install smoke in CI or the documented equivalent. Add a separate TSan job when concurrency-focused code and tests exist, as agent/task.md directs. Protocol, persistence, TLS, E2E, fuzz, and performance jobs become required as their features land. A scanner invocation without a reviewed result does not satisfy the check.

## Components and ownership boundaries

| Component | M0 owner | Allowed dependency | Excluded behavior |
|---|---|---|---|
| Support values/policy | spoink_support | C++17 library, narrow Linux RAII adapter | Network, file-transfer state, cryptography, SQLite, codec policy |
| CLI/package smoke | spoink executable | Support and build-version metadata | Pairing, send/receive, state initialization, doctor |
| Test harness | Unit target | Support and pinned test framework | Production logic duplicated in tests |
| Build/quality | CMake/presets/CI | Pinned toolchain/dependencies | Changes to accepted ADRs or M1-M5 product behavior |

The Builder may choose private filenames and local C++ API details while preserving these boundaries and the approved IC-001-012 conventions. Do not freeze wire, storage, or additional CLI behavior in M0.

## Interfaces, data flow, and persistent state

The M0 runtime path is spoink version to build/version metadata to stdout and successful exit. Unit tests call the support target directly. No application persistence, transfer session, manifest, or network data flow exists in M0. The vcpkg baseline and presets are repository-controlled build state, not Spoink data schemas. There is no migration. Unimplemented commands must not claim success.

## Exact implementation sequence for the Builder

1. Record starting Git status and preserve existing uncommitted documentation edits. Read agent/task.md and the approved sources cited here.
2. Select a working Linux x86-64 GCC/Clang, CMake, Ninja, and vcpkg toolchain. Pin exact versions/baseline, select a test framework, verify required dependency availability and licenses, and record evidence. Resolve TBD-ENG-DEP-001 consistently in the relevant documentation; leave later milestone experiments and limits unresolved at their stated gates.
3. Add root CMake configuration, install rules, version metadata, developer/CI/sanitizer/release configure/build/test presets, and the minimal spoink version path. A fresh build must not depend on developer-local package-manager paths.
4. Add the support target with Result/Error, checked arithmetic/conversion, strong types, injectable time, and narrow RAII ownership.
5. Add focused tests for error propagation/metadata; overflow, underflow, and narrowing; type and representation invariants; fake-time advancement; and cleanup, move, reset, and error paths for actual resource wrappers.
6. Add .clang-format and .clang-tidy, reproducible check commands, GCC/Clang debug and release-representative CI, ASan+UBSan, tests, install smoke, dependency/secret checks, and documentation relative-link/ID validation. Document the TSan trigger for concurrent code/tests.
7. Document exact fresh-checkout bootstrap, configure, build, ctest, install, and version-smoke commands. Update traceability to actual M0 paths/tests and update the engineering, operations, consistency, and readiness records for the selected toolchain/dependencies.
8. Run the documented commands on the supported Linux baseline. Inspect Git status/diff for unrelated changes. Write agent/builder/implementation-report.md for M0-001 with changed files, versions/licenses, commands/results, deviations, and remaining limits. Submit to independent Reviewer verification.

## Files expected to change

- Build/dependencies: CMakeLists.txt, CMakePresets.json, cmake/ as needed, vcpkg.json, a repository-controlled vcpkg pin/bootstrap artifact, and .gitignore only for new generated outputs.
- Source/tests: include/spoink/support/, src/support/, a minimal src/cli/ or equivalent executable entry point, and tests/unit/. Other module/test directories are optional until used.
- Quality: .clang-format, .clang-tidy, a checked-in CI workflow such as .github/workflows/, and narrowly scoped documentation/scan scripts or configuration.
- Documentation: docs/16_operations/16_01_local_development_and_deployment.md or an appropriate root developer README for actual commands; docs/13_engineering/13_01_engineering_standards.md, docs/00_governance/00_03_traceability_matrix.md, docs/24_consistency_review.md, and docs/25_implementation_readiness_review.md for actual paths and dependency/toolchain resolution. Update other approved documents only if an M0 choice affects them.
- Handoff: the Builder updates its own agent/builder/implementation-report.md for this Task ID. This Architect does not edit it.

These are expected targets, not a requirement to create unused files. All roles must use this same checkout under agent/task.md; the current plan and existing handoffs are tracked despite the untracked .gitignore pattern.

## Compatibility and migration considerations

M0 creates no wire or disk format and needs no data migration. Treat shared support headers as source-level internal contracts; do not promise a stable installed C++ ABI. Any later serialized ID, error, or hash representation follows the appropriate contract and compatibility process. Do not embed absolute developer paths in build artifacts or instructions. Record a controlled update procedure for pinned tools/dependencies.

## Security and failure modes

Dependencies are acquired at build time only; runtime must not download code. Verify provenance, licenses, versions, and scanner findings. Do not commit secrets, local state, or generated build outputs. Unsupported compiler/dependency configurations fail clearly. Checked arithmetic returns failure before overflow, narrowing, or allocation. RAII wrappers release exactly once and leave moved-from objects inert. Local diagnostics remain separate from safe public messages. Fix sanitizer/analysis failures or document narrow reviewed suppressions under the engineering rules.

## Acceptance criteria and required verification

1. Fresh Linux x86-64 checkout completes documented cmake --preset dev, cmake --build --preset dev, ctest --preset dev, install, and spoink version smoke without a hidden local prerequisite.
2. Developer, CI, ASan+UBSan, and release presets work on documented platforms. GCC and Clang CI compile project code as C++17 with warnings and warnings-as-errors; third-party warnings are external.
3. Focused support tests cover meaningful success/error, arithmetic boundaries, type invariants, fake time, and actual owned-resource cleanup. ASan+UBSan pass.
4. Format, clang-tidy, tests, dependency/secret, docs-link/ID, and install checks are reproducible and in CI or an equivalent documented gate. TSan is deferred until concurrency tests exist.
5. Repository-controlled artifacts record exact toolchain/dependency versions or baseline, licenses, and notices; TBD-ENG-DEP-001 is resolved with working-build/license evidence.
6. No transfer behavior, persisted schema, protocol format, trust-policy change, CDC/compression/scheduler implementation, performance result, or v1 release claim is introduced. Existing uncommitted edits are preserved.
7. Builder reports actual work and commands. A separate Reviewer inspects the repository and reports PASS or FAIL with evidence. Architect closure follows review, not this plan alone.

## Explicit assumptions

- A1, verified basis: Current documentation statuses are the Owner-approved authority for M0, as agent/task.md states; the external roadmap remains input evidence.
- A2, implementation assumption: The Builder can obtain a supported Linux x86-64 CI environment and usable pinned vcpkg baseline. Concrete port/license/build evidence may invoke the already approved alternate pinned mechanism; a material change to an accepted dependency or platform boundary returns to the Architect.
- A3, implementation assumption: A minimal spoink version command is enough for M0 installed-binary smoke. Other CLI commands belong to later milestones.
- A4, scope assumption: M0 support C++ APIs may evolve before first external use while preserving approved result/error, ownership, type-safety, and dependency conventions.

## Builder instructions and boundaries

Build M0 autonomously within this plan and the approved corpus. Choose exact tool and library versions, test framework, scanner, private file layout, and naming details from working evidence; record them. Preserve the dirty worktree. Return to the Architect only if concrete evidence requires changing an accepted ADR, supported platform, trust model, public contract, or material dependency/license boundary. Do not implement M1-M5 behavior to populate skeletons or CI jobs.

## Architect closure — M0-001

- Reviewed revision: remote main `863bebeb8c69ccd08139994f870f0c8e6f2eb493`; local `HEAD` and cached `origin/main` match. Its tree matches hosted PR head `44e2b14fabfcf577388b38c91b8ef61ef175b6bf`.
- Verdict: ACCEPTED for architectural scope following independent Review iteration 9 `APPROVED` of Builder iteration 6. The reviewed implementation remains the M0 C++17 build, quality, and support foundation; no M1+ runtime behavior, wire or storage format, or trust-policy change was introduced.
- Documentation status: M0 dependency/toolchain and license records, CI gates, and `TBD-ENG-DEP-001` resolution align with the approved M0 plan. No ADR or product decision is needed to close M0-001. The Owner updates task state and preserves the Reviewer report.
- Open non-blocking risks: GitHub Actions still use mutable major-version tags; checkout v4 has a Node.js 20 deprecation warning; the CXX-only preset has a harmless unused C compiler variable warning; the committed tidy header gate could use the independently passing include-based probe. These are follow-up quality improvements, not M0 blockers.
- M1 handoff gate: M1 may begin after Owner closure under `docs/19_planning/19_01_milestones.md`. Keep CDC parameterized until `EXP-CDC-001` establishes and records the `rabin-v1` profile and golden vectors. M1 must independently review path safety, SQLite schema/migrations, content-store durability and recovery, leases, reconstruction, and safe GC against its acceptance tests. Do not treat M0 approval as evidence for M1 behavior.
