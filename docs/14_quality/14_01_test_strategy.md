# Test Strategy

> Status: Approved

## Quality model

Correctness, recovery, security, and bounded resource behavior are release gates. Performance results are evidence and regression signals; unestablished roadmap speedups are not pass/fail gates.

## Test layers

### Unit tests

- CDC rolling fingerprint, boundary rules, golden vectors, empty/small/maximum chunks.
- SHA-256 wrappers and manifest digests against published/library vectors.
- Path normalization/containment and Unicode validation.
- Deterministic CBOR encode/decode and schema validation.
- Compression round-trip, skip/benefit decisions, output bounds, codec errors.
- Scheduler pure policy, bounds, state transitions, fairness, fake time.
- Error mapping, checked arithmetic, configuration merge/validation.
- State-machine transitions and idempotency.

### Property and model tests

- `TEST-PROP-001`: For arbitrary byte strings and valid CDC profiles, concatenated chunks equal input exactly; chunks are contiguous and deterministic.
- `TEST-PROP-002`: For arbitrary manifest/reference create/delete sequences, recomputed references match stored counts and GC never selects referenced/leased chunks.
- Manifest encode/decode round-trip preserves canonical bytes and rejects non-canonical alternatives.
- Arbitrary valid chunk order/duplicate delivery produces the same final verified store/session state.
- Path generator never resolves outside the destination root.

### Fuzz tests

- 32-byte prefix/framing stream parser, including partial/coalesced TCP reads.
- CBOR message and manifest decoders.
- Compression decoders with exact output limits.
- Path/Unicode parser.
- Session state machine message sequences.

Fuzz failures become permanent regression corpus entries. Sanitizers are enabled.

### Integration tests

| ID | Scenario |
|---|---|
| TEST-INT-001 | Pair two ephemeral identities, pin, reconnect, and transfer an empty/small file. |
| TEST-INT-002 | Send multiple files and recursively selected directory content with deterministic paths. |
| TEST-INT-003 | Receiver rejects one path/existing destination while permitted files follow documented partial behavior. |
| TEST-INT-004 | Control and multiple data TLS connections bind to one session and enforce limits. |
| TEST-INT-005 | Mixed raw/LZ4/Zstd chunks reconstruct exactly; unsupported codec negotiates away. |
| TEST-INT-006 | Concurrent sessions and mixed file sizes remain fair and bounded. |
| TEST-INT-007 | Two files/sessions reuse the same stored chunk; deletion/GC preserves live reference. |
| TEST-INT-008 | Peer revocation blocks normal and resumed sessions without deleting files. |
| TEST-INT-009 | Dry-run/actual GC and store inspection obey reference/lease rules. |

### Fault and recovery tests

| ID | Injection / expected property |
|---|---|
| TEST-FAULT-001 | Drop each control/data connection at multiple points; only unacknowledged work retries and session resumes. |
| TEST-FAULT-002 | Terminate before/during/after chunk temp write, flush, rename, DB commit, ACK; reconcile safely. |
| TEST-FAULT-003 | Terminate during staging/reconstruction/final rename; no partial final file. |
| TEST-FAULT-004 | Simulate disk-full and permission failures; stop ACKs and preserve durable state. |
| TEST-FAULT-005 | Lose ACK and resubmit identical chunk; one blob/reference state. |
| TEST-FAULT-006 | Corrupt/delete a verified blob; quarantine/invalidate and never complete from it. |

### Security tests

| ID | Scenario |
|---|---|
| TEST-SEC-001 | Pairing MITM proxy creates different TLS sessions; SAS differs. |
| TEST-SEC-002 | Unknown, revoked, wrong-key, expired-under-policy, and mismatched message/certificate identities fail. |
| TEST-SEC-003 | Wrong/expired/session-crossed capability and excessive data connections fail. |
| TEST-SEC-004 | Revocation/identity reset workflows require confirmation and leave no secret output. |
| TEST-SEC-005 | Traversal, absolute, NUL/control, invalid UTF-8, duplicate, and symlink-race paths fail. |
| TEST-SEC-006 | Oversized lengths/counts, malformed CBOR, decompression bombs, and connection flood stay bounded. |
| TEST-SEC-007 | Logs/reports scan clean for test private keys, capabilities, and payload markers. |

### End-to-end tests

- `TEST-E2E-001`: Initialize/pair through real loopback TLS and pinned reconnect.
- `TEST-E2E-002`: Direct multi-file send between isolated peer processes; all SHA-256 values match.
- `TEST-E2E-003`: Unchanged and edited re-transfer; receiver requests only verified-missing chunk hashes.
- `TEST-E2E-004`: Existing destination reject and explicit atomic overwrite, including injected failure.
- `TEST-E2E-005`: Kill sender and receiver independently during transfer; restart and resume to identical outputs.

### Compatibility tests

- Golden frame bytes and deterministic manifest CBOR/digests checked on GCC/Clang and debug/release builds.
- Current reader handles every retained protocol-minor/schema fixture promised by support policy.
- Unsupported major/schema fails with stable error, not crash or reinterpretation.
- Schema migration fixtures start from every supported prior release DB.

### Observability tests

| ID | Scenario |
|---|---|
| TEST-OBS-001 | TTY, non-TTY text, and JSONL modes preserve stable fields/units and contain no cursor-control noise in non-TTY output. |
| TEST-OBS-002 | Transfer-summary byte/chunk/retry counters reconcile with protocol/store evidence; unavailable measurements are null/unavailable rather than zero. |
| TEST-OBS-003 | Secret/content/path markers do not enter prohibited logs; bounded telemetry overload reports drops without blocking correctness. |

### Performance tests

| ID | Scenario |
|---|---|
| TEST-PERF-001 | Static versus adaptive transfer under controlled delay/rate/disconnection shaping, including fairness and reaction timeline. |
| TEST-PERF-002 | Pipeline/store/concurrent-session throughput plus CPU, peak RSS, sockets, queues, and temporary-space bounds. |
| TEST-PERF-003 | Compression modes/routing across the declared corpus, measuring wall time, CPU, and encoded bytes. |
| TEST-PERF-004 | Store lookup/reconstruction/GC with representative manifest/chunk counts and retained correctness. |

Performance regressions use baseline tolerances established from stable CI hardware; no fixed threshold is invented before baseline collection.

## What does not require direct testing

- Trivial accessors or compiler-generated operations without logic.
- Third-party library internals already covered upstream; Spoink wrappers and configuration/error handling still require tests.
- Pure formatting details unless they are machine-readable CLI contracts or accessibility behavior.

## Test data

- Generated deterministic data with recorded seeds.
- Small redistributable fixtures with provenance/license.
- Sparse/huge logical cases simulated where allocating real size is unnecessary.
- No personal files, real private keys, or secrets committed.
- Certificates/keys used in tests are clearly marked fixtures and never valid outside tests.

## Release gates

1. Formatting, compilation, warnings, static analysis, and relevant docs checks pass.
2. Unit/property/integration/E2E/fault/security/compatibility suites pass.
3. Required sanitizer and fuzz smoke jobs pass; no unresolved crash/corruption finding.
4. Dependency/security scan has no unaccepted release-blocking finding.
5. Linux package/install smoke test and fresh two-peer walkthrough pass.
6. Traceability and migration documentation are current.
7. Independent Reviewer returns PASS with evidence.

Flaky tests are defects. Quarantine requires owner, linked issue, bounded expiration, and cannot hide a release-gate security/correctness scenario.
