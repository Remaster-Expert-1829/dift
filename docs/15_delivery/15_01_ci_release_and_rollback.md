# CI, Release, and Rollback

> Status: Approved

## Pull-request pipeline

Jobs may run in parallel after configuration succeeds.

1. **Repository checks:** forbidden secrets/artifacts, formatting, documentation links/IDs, CMake configure, dependency lock validation.
2. **Build matrix:** Linux GCC and Clang, debug and release-representative builds.
3. **Static analysis:** compiler warnings-as-errors and clang-tidy on project code.
4. **Unit/property/integration:** deterministic test suites with generated seeds recorded.
5. **Sanitizers:** ASan+UBSan; separate TSan concurrency subset.
6. **Protocol compatibility:** golden frames/manifests and retained database migration fixtures.
7. **Security/fuzz smoke:** bounded fuzz duration on parsers/path/decompression/state machine; dependency and secret scan.
8. **End-to-end:** two isolated local peer processes with TLS, multi-file transfer, delta, and resume fault injection as features land.
9. **Package smoke:** install into a clean environment, run `version`, `doctor`, initialize two temporary states, and execute minimal transfer.

Performance smoke tests detect catastrophic regressions but do not gate on unbaselined roadmap numbers. Stable-hardware scheduled jobs hold statistical performance baselines.

## Main/nightly pipeline

- Longer fuzz/property/chaos runs.
- Full store/migration/recovery corpus.
- Network-namespace shaped scenarios.
- Dependency vulnerability/license report.
- Benchmark trend on controlled runner when available.

Failures create artifacts containing logs, seeds, reports, and core/sanitizer diagnostics after automated secret/path scrubbing.

## Versioning

- Application releases use semantic versioning.
- Wire protocol has independent `(major, minor)` values.
- Manifest and database schema versions are independent integers.
- A release notes compatibility across application, protocol, manifest, and DB versions.
- Pre-1.0 may change compatibility only through explicit ADR/migration notes; “pre-1.0” is not permission for silent reinterpretation.

## Release process

1. Freeze scope and update traceability/readiness review.
2. Pin dependency/toolchain versions and generate notices/SBOM.
3. Run all release gates from `14_01_test_strategy.md` on the release commit.
4. Exercise upgrade/migration from every supported prior release and test backup restore.
5. Build reproducible release artifacts where supported; record checksums and provenance.
6. Perform a clean two-device pairing/send/resume/overwrite/GC walkthrough.
7. Independent Reviewer returns PASS.
8. Tag and publish binary/source packages, checksums, notices, release notes, known issues, and rollback guidance.

Signing release artifacts is recommended and becomes mandatory before public distribution is advertised as trusted. Key custody requires a separate operational procedure.

## Database migration

- Startup never mutates an unknown/newer schema.
- Supported old schema is backed up using SQLite's safe backup process before migration.
- Each migration is transactional where SQLite permits and has checksum, forward test, and failure recovery test.
- Chunk payload transformations, if ever needed, use resumable explicit maintenance jobs rather than one enormous DB transaction.
- Downgrade that cannot read a newer schema is blocked with guidance; it never guesses.

## Rollback

Because DeltaSync is local software, rollback means reinstalling a prior compatible binary and restoring metadata only when schema compatibility requires it.

- If the prior binary supports the current DB/protocol, stop processes and reinstall it.
- If not, restore the pre-migration SQLite backup and matching chunk-store snapshot/consistency point, then run `doctor`/reconciliation.
- Final received files are not rolled back automatically.
- Identity files must not be regenerated during rollback; otherwise peers require re-pairing.
- A protocol regression can be mitigated by deploying the last compatible binary on both peers; mixed-version claims require compatibility tests.

## CI secrets and permissions

- Tests use generated fixture identities only.
- CI does not require production/private user keys.
- Privileged `tc netem`/namespace jobs run on isolated trusted runners and are separated from untrusted contribution contexts.
- Dependency tokens/signing keys, when introduced, are least-privilege and unavailable to ordinary PR jobs.
