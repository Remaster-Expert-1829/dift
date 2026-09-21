# Project Milestones

> Status: Approved sequencing; estimates are intentionally not treated as commitments.

## M0 — Repository and quality foundation

**Objective:** Create a reproducible C++17 project skeleton and enforce core engineering gates.

**Prerequisites:** Approved documentation corpus.

**Deliverables:**

- CMake targets/presets and pinned dependency manifest.
- Module/test directory skeleton without premature public APIs.
- Error/result, checked-size, ID/hash, fake-clock, and RAII support foundations.
- Formatting, lint, GCC/Clang, sanitizer, unit-test, secret/dependency scan CI.
- Documentation link/ID check.

**Acceptance:** Fresh Linux checkout can configure, build, test, and install through documented commands; CI matrix passes; exact versions/licenses recorded.

**Tests:** Build/package smoke and foundation unit tests.

**Documentation changes:** Pin toolchain/dependencies and resolve `TBD-ENG-DEP-001`.

## M1 — Offline content and durable-store engine

**Objective:** Deterministically manifest, store, reconstruct, verify, and garbage-collect multiple files without networking.

**Prerequisites:** M0.

**Deliverables:**

- Safe multi-file/directory selection and normalized paths.
- Rabin CDC parameterization, SHA-256, canonical CBOR manifest, golden vectors.
- SQLite schema/migrations and content-addressed chunk store.
- Durable commit/reconciliation, leases, staging/finalization, inspect/dry-run GC.
- `EXP-CDC-001` results and frozen `rabin-v1` profile.

**Acceptance:** Offline tool/test can manifest a corpus, ingest chunks, reconstruct all files byte-identically, survive injected crash points, reuse duplicate chunks, and safely GC only unreferenced data.

**Tests:** `TEST-PROP-001/002`, store/path unit and fuzz suites, `TEST-FAULT-002/003/004/006`, relevant integration tests.

**Documentation changes:** Record CDC profile/defaults/golden vectors, initial schema SQL/migration, filesystem durability assumptions, and resolved storage limits.

## M2 — Secure direct-transfer vertical slice

**Objective:** Pair two devices and transfer one regular file over a single TLS control/data path with durable ACK and atomic completion.

**Prerequisites:** M1; resolve `TBD-SEC-KEY-001` and required `TBD-LIMITS-001` parser/handshake limits.

**Deliverables:**

- Identity initialization and permission checks.
- Pairing ALPN, TLS exporter SAS, pinning/revocation.
- Normal mTLS, fixed prefix, CBOR message validation, protocol state machine.
- Interactive receive/send with raw codec and one data connection.
- Durable chunk ACK, whole-file verification, final result.

**Acceptance:** Two isolated processes initialize/pair/reconnect and transfer empty/small/large single files; unknown/changed peers and corrupt/malformed frames fail safely; no partial final file.

**Tests:** `TEST-SEC-001–007`, parser fuzz, `TEST-INT-001/004`, `TEST-E2E-001/002/004` single-file subset.

**Documentation changes:** Freeze protocol-major-1 golden frames, CLI examples, key policy, absolute parser ceilings, and operational pairing runbook.

## M3 — Multi-file delta, deduplication, and resume

**Objective:** Deliver the core product value for multiple files with persistent receiver state.

**Prerequisites:** M2.

**Deliverables:**

- Batch offers/approval/subset/path reservation and partial result semantics.
- Paged manifest/transfer plan and exact missing-chunk selection.
- Local cross-transfer deduplication.
- Idempotent retry, connection/session restart, resume with new capability.
- Concurrent session coordination and destination conflict rejection.

**Acceptance:** Multi-file/directories transfer; unchanged re-transfer sends zero chunk payload; edited re-transfer sends exact missing hashes; sender/receiver kills resume without retransmitting durable ACKs; concurrent destination conflict is deterministic.

**Tests:** `TEST-INT-002/003/006/007`, `TEST-FAULT-001/005`, `TEST-E2E-002/003/005`, compatibility/migration fixtures.

**Documentation changes:** Trace actual error codes/state transitions, schema/query/index evidence, and limits.

## M4 — Compression, parallel data pool, and adaptation

**Objective:** Add bounded performance features without changing logical correctness.

**Prerequisites:** M3 and stable telemetry counters.

**Deliverables:**

- LZ4/Zstd/raw negotiation and safe bounded decoding.
- Entropy/benefit estimator and `off/lz4/zstd/auto` modes.
- Bounded multi-connection data pool and global/per-session resource arbiter.
- Static and adaptive scheduler state machine, fair queue, retries/backpressure.
- `EXP-COMP-001` and `EXP-SCHED-*` results/config profiles.

**Acceptance:** Mixed-codec transfers reconstruct exactly; bounds hold under concurrent load; scheduler tests are deterministic/no-starvation; network shaping produces explainable decisions and static fallback; no performance claim is required to be positive.

**Tests:** Compression/scheduler unit/property, `TEST-INT-005/006`, shaped-network fault/performance tests, TSan.

**Documentation changes:** Record experimental profiles/defaults and any evidence-driven policy change; ADR required if connection model changes.

## M5 — Hardening, observability, benchmarks, and release candidate

**Objective:** Produce a reviewable, reproducible portfolio-quality v1 release candidate.

**Prerequisites:** M0–M4.

**Deliverables:**

- Text/JSONL progress, reports, audit events, doctor/recovery UX.
- Retention and safe GC execution defaults.
- Complete security/fault/fuzz/compatibility suites.
- Benchmark corpus/scenarios/raw outputs/analysis and honest comparison tables.
- Packaging, SBOM/notices, install/upgrade/rollback documentation.
- README/demo based only on measured results.

**Acceptance:** All release gates pass; fresh two-device walkthrough passes; every public performance claim links to reproducible evidence; consistency/readiness review has no release blocker; independent Reviewer returns PASS.

**Tests:** Entire strategy and release checklist.

**Documentation changes:** Final measured results, limitations, migration support, risk status, traceability, consistency/readiness verdict.

## Dependency graph

```text
M0 → M1 → M2 → M3 → M4 → M5
```

M4 features can be individually deferred without invalidating a correct M3 core, but documentation/claims must reflect the reduced scope. Security and durable correctness in M1–M3 cannot be deferred to preserve a schedule.
