# Definition of Done

> Status: Approved

## Feature DoD

A feature is done only when all applicable items pass:

- Approved requirements and acceptance criteria are satisfied and linked.
- Implementation matches accepted ADRs, architecture boundaries, data invariants, and contracts.
- No architecture was silently introduced through code.
- Input/resource validation and documented error/retry behavior exist.
- Unit and appropriate property/integration/E2E/fault/security tests pass.
- Thread/resource/cancellation behavior is bounded and tested when applicable.
- Persistent changes have migration, rollback/recovery, and crash tests.
- Protocol changes have versioning and golden/compatibility tests.
- Security/privacy/logging restrictions are met; no secrets/test personal data are committed.
- Required telemetry exists with stable units and no sensitive leakage.
- User/operator/agent documentation and traceability are synchronized.
- Formatting, build, lint/static analysis, sanitizers, and relevant fuzz jobs pass.
- Builder report is complete and independent Reviewer returns PASS.
- Remaining limitations/TBDs are explicit and do not block that feature's approved scope.

## Transfer correctness DoD

A transfer feature is not done unless:

- completed files are byte-identical and whole-file hash verified;
- invalid/missing/reordered/corrupt chunks cannot finalize;
- incomplete work is not visible at final destination;
- retry is idempotent and bounded;
- crash points map to documented recovery;
- unknown/revoked peers and wrong session capabilities fail;
- output/exit status does not report success for partial/paused/failure states.

## Performance-feature DoD

Compression/scheduler/parallelism work additionally requires:

- correctness parity with static/raw/single-connection controls;
- resource limits and static fallback;
- telemetry explaining decisions and costs;
- reproducible benchmark configuration/raw data;
- claim wording based on actual evidence, including negative results.

No specific speedup is required unless a later approved requirement adds one with evidence.

## Milestone DoD

- All milestone deliverables and tests pass.
- Entry conditions for the next milestone are resolved.
- Risks and readiness/consistency status are updated.
- No unowned critical/high-severity defect remains in the delivered scope.
- Demonstration starts from a clean documented setup, not developer-only hidden state.

## Release DoD

- Entire release gate in `14_01_test_strategy.md` passes.
- Exact dependencies/toolchain and licenses/SBOM are published.
- Fresh install, identity, pairing, multi-file send, delta, resume, overwrite, revoke, doctor, backup/restore, and rollback walkthroughs pass as applicable.
- Protocol/schema compatibility and migrations pass for every supported version.
- Security review resolves `TBD-SEC-KEY-001` and `TBD-LIMITS-001` and has no unaccepted blocker.
- Performance claims link to benchmark raw evidence/methodology.
- Consistency review has no unresolved contradiction.
- Implementation readiness/release review is READY.
- Independent Reviewer returns PASS and Architect approves release.
