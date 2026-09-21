# Implementation Readiness Review

> Review date: 2026-09-22

## Documentation Status

**READY WITH CONDITIONS**

An independent developer or coding agent can begin M0 and the parameterized/offline parts of M1 without asking what the product or architecture means. The conditions below are deliberately assigned to milestones and block only the affected freeze/release point.

## What is implementation-ready

- Product: direct LAN sender/receiver roles, multiple regular files, explicit exclusions, destination/partial semantics.
- Trust: paired devices, TLS 1.3, SAS bootstrap, pins/revocation, plaintext receiver store limitation.
- Architecture: components, ownership, data/control flow, concurrency/backpressure, no permanent server.
- Data: manifest canonicalization, SQLite entities/invariants, blob-store commit/reconciliation, lifecycle/GC.
- Contracts: pairing, negotiation, session offer, manifest, delta plan, chunk send/ACK, resume, errors, CLI, internal boundaries.
- Failures: retry/fallback/abort/degrade/notify/log behavior for all relevant non-AI failures.
- Verification: correctness/security/fault/compatibility/performance strategy and release gates.
- Delivery: milestones, CI, operations, review workflow, risk register, and DoD.

## Critical gaps

No unresolved product-owner decision currently blocks M0/M1. The following engineering decisions/experiments must be completed at their gates:

1. `TBD-ENG-DEP-001` — actual pinned compiler/CMake/vcpkg/dependency versions and licenses.
2. `EXP-CDC-001` — exact `rabin-v1` polynomial/window/min/target/max parameters and golden vectors.
3. `TBD-SEC-KEY-001` — permitted device key algorithm/strength for the selected OpenSSL/toolchain.
4. `TBD-LIMITS-001` — parser, manifest, path, connection, in-flight, timeout, and handshake ceilings/defaults.
5. `EXP-COMP-001` — initial auto-compression profile.
6. `EXP-SCHED-001–006` — adaptive policy defaults.
7. `TBD-OPS-RETENTION` — completed/paused/failed session, unreferenced chunk, log, and quarantine retention defaults.

Each has a documented owner, evidence, and milestone. Values must be committed to configuration/golden fixtures and reflected here when resolved.

## Open decisions

- Whether future versions add automatic LAN discovery, non-interactive receive rules, internet NAT traversal/relay, at-rest encryption, GUI/mobile clients, pull-style browsing, or multi-source swarming. All are outside v1 and do not block it.
- Whether source snapshot integrations are added for highly mutable files. V1 detects change and fails safely.
- Exact application packaging/signing mechanism for public distribution; required before claiming a trusted public binary release, not before core implementation.

## Architecture risks

Highest current risks are pairing correctness (`RISK-002`), filesystem/database crash consistency (`RISK-003`), path containment (`RISK-004`), custom parser safety (`RISK-010`), adaptive-policy instability (`RISK-006`), and benchmark validity (`RISK-012`). Mitigations and contingencies are testable and milestone-linked.

## Implementation blockers

- **M0:** None.
- **M1 core:** None; CDC must be parameterized. `rabin-v1` cannot be declared frozen until EXP-CDC-001.
- **M2 secure protocol freeze:** blocked until `TBD-SEC-KEY-001` and required parts of `TBD-LIMITS-001` are resolved.
- **M4 default adaptive/auto behavior:** blocked until the related experiments complete; static/raw modes can be implemented earlier.
- **Public v1 release:** blocked until every condition above, release gate, consistency review, and independent Reviewer PASS are complete.

## Questions an implementer should not need to ask

| Question | Answer location |
|---|---|
| What are we building? | Charter and requirements. |
| Is there a central server? | ADR-001 and system overview: no. |
| What data exists and who owns progress? | Domain/data docs: receiver durable state is authoritative. |
| What does the protocol accept/return? | Wire contract API-001–011. |
| What happens on failure? | Reliability matrix and runtime flows. |
| How are devices trusted? | ADR-003 and security document. |
| Can the scheduler rechunk data? | ADR-002: no. |
| What must be tested? | Test strategy, milestones, DoD, traceability. |
| What is configurable/experimental? | CLI/config registry, scheduler, benchmark docs, conditions above. |
| What remains undecided? | Critical gaps/open decisions in this review. |

## Horizontal traceability examples

```text
FR-018 interrupted-session resume
  → ADR-004 hybrid durable store + ADR-005 connection model
  → ARCH-02 session controller + ARCH-08 recovery coordinator
  → DB-002 transfer_sessions + DB-006 session_chunks
  → API-009 resume contract
  → src/session + src/store + src/protocol
  → TEST-FAULT-001/002/005 + TEST-E2E-005
```

```text
FR-013 adaptive scheduling
  → ADR-002 stable chunks + ADR-005 bounded connection pool
  → ARCH-06 transport/scheduler
  → API-010 flow control + CFG-SCHED-001 + EXP-SCHED-001–006
  → src/scheduler + src/transport
  → scheduler pure-policy/no-starvation/shaped-network tests
```

```text
FR-002 first-time pairing
  → ADR-003 TLS identity
  → ARCH-01 identity/trust
  → DB-001 trusted_peers
  → API-001 + CLI-002 + IC-001
  → src/identity + src/pairing
  → TEST-SEC-001/002/004 + TEST-E2E-001
```

## Recommended next step

Begin M0. In parallel with the build skeleton, prepare the M1 CDC experiment harness and record exact dependency/toolchain selections. Do not implement networking before the parser limits/key policy are reviewed, and do not reuse any roadmap performance number as a result.
