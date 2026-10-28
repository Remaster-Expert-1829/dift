# Task M0-001 — Repository and quality foundation

- **Status:** COMPLETED_ACCEPTED
- **Milestone:** M0
- **Owner:** This chat
- **Repository:** `E:/programs/c++/dift`

## Objective

Implement M0 from `docs/19_planning/19_01_milestones.md` for Spoink. Establish a reproducible C++17 Linux build and quality foundation that supports later milestones without implementing file-transfer behavior now.

## Source of truth

Read `docs/README.md`, the approved charter and ADRs, `docs/13_engineering/13_01_engineering_standards.md`, `docs/14_quality/14_01_test_strategy.md`, `docs/15_delivery/15_01_ci_release_and_rollback.md`, `docs/17_workflow/17_01_development_workflow.md`, `docs/18_agents/18_01_coding_agent_workflow.md`, `docs/19_planning/19_01_milestones.md`, `docs/21_definition_of_done.md`, and `docs/25_implementation_readiness_review.md`. The roadmap outside this repository is input evidence, not a binding specification.

## Scope

1. Create CMake targets and presets for developer, CI, sanitizer, and release configurations.
2. Pin the toolchain and dependency mechanism, record exact versions and licenses, and resolve `TBD-ENG-DEP-001` according to the approved engineering standards. Prefer the documented vcpkg approach unless evidence warrants the documented alternative.
3. Create the module and test directory skeleton without freezing premature public APIs.
4. Implement the M0 support foundations: error/result, checked-size arithmetic, ID/hash types, fake clock, and RAII utilities. Keep domain and pure-policy code independent of adapters.
5. Add formatting, static analysis, GCC/Clang, sanitizer, unit-test, secret/dependency scan, and documentation link/ID checks in CI as applicable to M0. Add a separate ThreadSanitizer job when the repository has concurrency-focused tests; do not fabricate such tests merely to populate a job.
6. Document fresh-checkout configure, build, test, and install commands. Keep documentation and traceability aligned with the implementation.

## Non-goals

- No CDC, manifest, chunk store, networking, pairing, transfer, compression, scheduler, telemetry, or benchmark implementation in this task.
- No claims of measured transfer performance or v1 release readiness.
- No change to approved product behavior, wire contracts, storage formats, trust boundaries, or ADRs without an explicit architectural decision.

## Acceptance criteria

1. A fresh Linux checkout can configure, build, test, and install with documented commands.
2. Developer, CI, sanitizer, and release CMake presets exist and are usable on their documented platforms.
3. GCC and Clang CI builds use C++17, enabled warnings, and warnings-as-errors for project code.
4. The M0 support foundations have focused tests for error mapping, checked arithmetic, type invariants, fake time, and owned-resource cleanup where behavior exists.
5. Formatting, static analysis, ASan/UBSan, unit tests, dependency/secret checks, and documentation checks run through CI or a documented equivalent appropriate to the initial skeleton. TSan applies when concurrency code is introduced.
6. Exact selected compiler, CMake, dependency versions/baseline, and licenses are recorded in repository-controlled artifacts and relevant documentation. `TBD-ENG-DEP-001` is updated consistently.
7. Reviewer independently inspects the actual repository state and reports PASS or FAIL with evidence against this task and the approved M0 milestone.

## Handoff rules

- Architect: assess whether M0 introduces architecture and update `agent/architecture/current-plan.md` for Task `M0-001`. The existing plan is stale and must not be used as current guidance.
- Builder: begin only after a matching `READY_FOR_BUILD` or `NOT_REQUIRED` Architect assessment; implement autonomously within the approved documentation. Update `agent/builder/implementation-report.md` for Task `M0-001`.
- Reviewer: inspect the actual diff, source, tests, build, CI, dependencies, and documentation; independently verify. Update `agent/reviewer/review.md` for Task `M0-001` with PASS or FAIL. Return findings to Builder until resolved.
- All three chats should use this same local checkout to see uncommitted handoffs immediately. The `agent/` directory is ignored by default, but the four handoff files are explicitly tracked so committed handoffs propagate to clones and worktrees.
- Preserve the existing uncommitted documentation edits and other user work. Do not overwrite an artifact unless updating it for this Task ID in your assigned role.

## Decisions and escalation

M0 has no unresolved product-owner decision according to `docs/25_implementation_readiness_review.md`. The Architect and Builder may make documented engineering choices within the approved architecture. Escalate a genuine product or architectural conflict to the Owner with exact evidence and options. Do not pause for routine implementation choices.

## Closure — 2026-10-28

- **Accepted implementation:** remote `main` revision `863bebeb8c69ccd08139994f870f0c8e6f2eb493` (tree-identical to final PR head `44e2b14fabfcf577388b38c91b8ef61ef175b6bf`).
- **Independent review:** Review iteration 9 approved Builder iteration 6. The complete findings and verification record are in `agent/reviewer/review.md`.
- **Architect closure:** The Architect accepted M0 scope and documentation alignment in `agent/architecture/current-plan.md`; no new ADR or product decision is required.
- **Hosted evidence:** GitHub Actions runs `40737223533` and `40738534004` passed the checks recorded by the Reviewer. The workflow uses `contents: read` and no pull-request write grant.
- **Next milestone:** M1 offline content and durable-store engine, subject to its own task, architecture assessment, experiments, and independent review. M0 approval does not certify M1 behavior.
