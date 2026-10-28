# Spoink Documentation Plan

> Status: **APPROVED WITH DIRECT-TRANSFER AMENDMENT — 2026-09-22**  
> Purpose: Establish the project classification, documentation structure, assumptions, unknowns, and required product/architecture decisions before implementation documentation is written.  
> Source reviewed: `E:\programs\c++\dift roadmap\dift-final-roadmap vibecode.html`  
> Interpretation rule: the source HTML is treated as project-description evidence. Its schedules, design suggestions, benchmark figures, and implementation notes are not automatically approved requirements or architectural decisions.

Sections 3–6 retain the initial planning record. Their unknowns/questions are superseded where section 7 records an approved resolution and by the normative corpus indexed in `docs/README.md`.

## 1. Project classification

**Classification: Tier 3 — Complex, scoped as a solo production-oriented prototype.**

Spoink combines a custom network protocol, content-defined chunking (CDC), delta transfer, compression routing, resumable state, concurrent transfer, adaptive scheduling, encryption, cross-transfer deduplication, persistence, observability, and reproducible performance evaluation. These concerns interact in ways that create distributed-systems, data-integrity, security, and recovery requirements even if the first release consists of only two executables.

The Tier 3 classification does **not** imply microservices, cloud infrastructure, a relational database, event sourcing, or enterprise deployment. The documentation will remain proportional to a solo C++ prototype and will omit sections that do not serve implementation.

### Classification boundaries

| Category | Current interpretation |
|---|---|
| Product | A file-transfer/synchronization engine named Spoink |
| Intended value | Avoid retransmitting unchanged content; avoid wasteful compression; sustain useful throughput under changing network conditions; resume interrupted work |
| Likely users | Developers, systems learners/reviewers, and operators transferring large files between controlled hosts |
| Delivery shape | Command-line peer application with temporary sender/receiver roles, plus benchmark and telemetry tooling |
| Complexity drivers | CDC stability, protocol semantics, concurrency, crash recovery, data integrity, cryptographic key/nonce management, deduplication, scheduling, and benchmark validity |
| Explicitly not inferred | SaaS, GUI, mobile clients, multi-region deployment, multi-tenant service, AI/ML, public internet service, or production SLA |

## 2. Proposed documentation structure

Only this planning document should exist until the structure and blocking decisions are approved. The following corpus is proposed for the implementation-ready phase.

```text
docs/
├── README.md
├── 00_governance/
│   ├── 00_01_project_charter.md
│   ├── 00_02_documentation_governance.md
│   └── 00_03_traceability_matrix.md
├── 01_requirements/
│   ├── 01_01_requirements.md
│   └── 01_02_use_cases_and_user_flows.md
├── 02_domain/
│   └── 02_01_domain_model_and_glossary.md
├── 03_decisions/
│   ├── README.md
│   └── ADR-NNN-<decision>.md
├── 04_architecture/
│   ├── 04_01_system_overview.md
│   ├── 04_02_components_and_runtime_flows.md
│   └── 04_03_adaptive_scheduler.md
├── 05_data/
│   ├── 05_01_manifest_and_chunk_store.md
│   └── 05_02_data_lifecycle_and_recovery_state.md
├── 06_contracts/
│   ├── 06_01_wire_protocol.md
│   ├── 06_02_cli_and_configuration.md
│   └── 06_03_internal_component_contracts.md
├── 10_security/
│   └── 10_01_security_privacy_and_threat_model.md
├── 11_reliability/
│   └── 11_01_failure_and_recovery_policy.md
├── 12_observability/
│   └── 12_01_telemetry_logging_and_audit.md
├── 13_engineering/
│   └── 13_01_engineering_standards.md
├── 14_quality/
│   ├── 14_01_test_strategy.md
│   └── 14_02_benchmark_methodology.md
├── 15_delivery/
│   └── 15_01_ci_release_and_rollback.md
├── 16_operations/
│   └── 16_01_local_development_and_deployment.md
├── 17_workflow/
│   └── 17_01_development_workflow.md
├── 18_agents/
│   └── 18_01_coding_agent_workflow.md
├── 19_planning/
│   └── 19_01_milestones.md
├── 20_risks/
│   └── 20_01_risk_register.md
├── 21_definition_of_done.md
├── 24_consistency_review.md
└── 25_implementation_readiness_review.md
```

### Document purposes

| Document | Purpose and boundary |
|---|---|
| `docs/README.md` | Corpus index, status legend, reading order, artifact-ID legend, and links; no duplicated design content. |
| `00_01_project_charter.md` | Approved problem, users, goals, non-goals, scope, constraints, success criteria, and assumptions. |
| `00_02_documentation_governance.md` | Source-of-truth hierarchy, normative language, ownership, approval/change process, and rule for resolving code/document conflicts. |
| `00_03_traceability_matrix.md` | Traces important `FR`/`NFR` items through ADRs, architecture, data, contracts, implementation modules, and tests. Initially contains planned implementation/test targets where code does not yet exist. |
| `01_01_requirements.md` | Testable functional and non-functional requirements. Separates committed requirements from target metrics and tunable parameters. |
| `01_02_use_cases_and_user_flows.md` | End-to-end first transfer, delta transfer, interrupted transfer/resume, integrity failure, and operator benchmark flows. |
| `02_01_domain_model_and_glossary.md` | Definitions, entities, relationships, invariants, lifecycle states, and authoritative terminology such as logical chunk, transport segment, manifest, transfer, object, and peer. |
| `03_decisions/*` | One ADR per significant approved decision. Candidate topics are listed below; ADRs will not be written as accepted before decisions are made. |
| `04_01_system_overview.md` | Context/component diagrams, ownership boundaries, external dependencies, main data flows, and deployment-neutral architecture. |
| `04_02_components_and_runtime_flows.md` | Responsibilities, interfaces, dependencies, sequencing, concurrency model, and failure behavior of the chunker, diff engine, store, transfer engine, reconstruction path, and control plane. |
| `04_03_adaptive_scheduler.md` | Scheduler inputs, actions, safety constraints, control loop, stable fallbacks, and separation of architecture from experimentally tuned policy. |
| `05_01_manifest_and_chunk_store.md` | Logical data model and persistence schema for manifests, chunks, transfer sessions, integrity metadata, indexes, and deduplication references. A relational schema is included only if an RDBMS is selected. |
| `05_02_data_lifecycle_and_recovery_state.md` | Creation, atomic updates, crash consistency, retention, garbage collection, deletion, reconstruction, ownership, and recovery checkpoints. |
| `06_01_wire_protocol.md` | Versioned framing and message contracts, byte order, bounds, state machine, capability negotiation, authentication, idempotency, retries, errors, and compatibility policy. |
| `06_02_cli_and_configuration.md` | Stable commands, arguments, output/exit behavior, configuration precedence, environment variables, secret handling, and classification of fixed versus tunable values. |
| `06_03_internal_component_contracts.md` | Important C++ interfaces and validation/ownership/error contracts without freezing class/file layout prematurely. |
| `10_01_security_privacy_and_threat_model.md` | Trust boundaries, peer authentication, authorization, key/nonce lifecycle, metadata integrity, path safety, resource exhaustion, deduplication leakage, logging restrictions, and abuse cases. |
| `11_01_failure_and_recovery_policy.md` | Retry/fallback/abort/rollback/user-notification behavior for network, protocol, storage, compression, cryptographic, reconstruction, and process/power failures. |
| `12_01_telemetry_logging_and_audit.md` | Metrics and structured events, units and labels, decision telemetry, benchmark observability, privacy rules, and diagnostic correlation. |
| `13_01_engineering_standards.md` | Supported language/toolchain, type and ownership rules, formatting/linting, dependency/configuration/error conventions, module boundaries, and review policy. |
| `14_01_test_strategy.md` | Unit, property/fuzz, integration, protocol/compatibility, crash-recovery, security, and end-to-end verification with release gates. |
| `14_02_benchmark_methodology.md` | Baselines, workload corpus, topology, network shaping, repetitions, warm-up/caching rules, measurements, statistical reporting, and prohibition on presenting estimates as results. |
| `15_01_ci_release_and_rollback.md` | Build matrix, static analysis, tests, packaging, versioning, promotion, artifact integrity, and rollback appropriate to a CLI prototype. |
| `16_01_local_development_and_deployment.md` | Reproducible host/container setup, required services and privileges, supported platforms, dependencies, storage layout, startup, backup, and recovery. |
| `17_01_development_workflow.md` | Repository organization, focused Git/PR expectations, issue/decision linkage, and documentation synchronization. |
| `18_01_coding_agent_workflow.md` | Architect/Builder/Reviewer responsibilities, allowed change sequence, evidence format, escalation rules, and PASS/FAIL review contract. |
| `19_01_milestones.md` | Dependency-aware milestones with deliverables, entry criteria, acceptance tests, documentation updates, and exit criteria. It may revise the source's five-week sequence but will not promise calendar dates without an explicit planning commitment. |
| `20_01_risk_register.md` | Project-specific risks, probability/impact, mitigation, detection, contingency, and owner/status. |
| `21_definition_of_done.md` | Project and feature completion gates tied to approved requirements, architecture, testing, security, documentation, and review. |
| `24_consistency_review.md` | Recorded cross-document contradiction audit; unresolved conflicts remain visible. |
| `25_implementation_readiness_review.md` | Final READY / READY WITH CONDITIONS / NOT READY decision, critical gaps, open decisions, risks, blockers, and next action. |

### Proposed significant ADRs

These are **decision topics**, not preselected outcomes:

- Transfer semantics and v1 scope: upload, download, mirroring, or bidirectional synchronization.
- Supported platforms and portability boundary.
- Logical CDC chunk model and separation from adaptive transport segmentation.
- Transport, connection/concurrency model, and protocol versioning.
- Trust model, peer authentication, and encryption boundary.
- Manifest/recovery metadata persistence and atomicity model.
- Chunk-store and deduplication scope, isolation, and garbage collection.
- Compression codec-routing policy boundary.
- Scheduler control and fallback model.
- Dependency selection and supply-chain policy where alternatives materially affect architecture.

### Sections intentionally omitted or consolidated

- **AI/ML architecture:** omitted; the described scheduler is deterministic/adaptive systems logic, not an AI/ML subsystem.
- **Memory/search/retrieval:** omitted; hash lookup and chunk retrieval belong to the data and component architecture.
- **Recommendation system:** omitted; scheduler decisions are documented in `04_03_adaptive_scheduler.md` without misclassifying them as recommendations.
- **Event model:** omitted unless the approved architecture introduces durable asynchronous events. Telemetry events are observability records, not automatically domain events.
- **Relational schema:** conditional on selecting a relational store.
- **Web/API specification:** omitted unless an HTTP/gRPC or public library API enters scope. The custom wire protocol, CLI, and important internal interfaces are the relevant contracts.
- **Cloud infrastructure:** omitted unless remote/cloud deployment becomes a requirement.
- **Separate deployment-architecture document:** consolidated into system overview and operations unless deployment complexity grows.

## 3. Important assumptions

All assumptions below are provisional and must be confirmed, replaced by a decision, or explicitly retained as assumptions in the charter.

| ID | Assumption | Classification | Confirmation path |
|---|---|---|---|
| ASM-001 | Spoink is currently a solo portfolio/research-grade prototype, not a public production service with an SLA. | Product/scope | User approval |
| ASM-002 | Version 1 is a command-line peer application. During a transfer one device sends and the other temporarily receives; no permanent central server exists. | Approved product/architecture decision | Direct-transfer approval |
| ASM-003 | Large regular files are the primary workload. Directory trees, symlinks, sparse files, permissions, and extended attributes are not assumed in scope. | Product | Scope decision |
| ASM-004 | Linux is at least the benchmark platform because the proposed test plan depends on `tc netem`; portability is unresolved. | Operational | Platform decision |
| ASM-005 | The source roadmap's performance figures are hypotheses or targets until reproduced, not current measured capabilities or guaranteed requirements. | Experimental | Benchmark protocol and measured results |
| ASM-006 | CDC parameters, entropy thresholds, codec policy, retry values, scheduler intervals, worker/connection limits, and context windows are configurable or experimentally determined unless a protocol invariant requires otherwise. | Configurable/experimental | Experiments plus ADR where needed |
| ASM-007 | SHA-256 identifies logical plaintext chunks for integrity/deduplication within one receiving device's trusted local namespace. | Approved architecture | ADR-002, ADR-004 |
| ASM-008 | Manifests and recovery state use SQLite; verified plaintext chunk payloads use a filesystem content-addressed store with crash reconciliation. | Approved architecture | ADR-004 |
| ASM-009 | Correct reconstruction and integrity take precedence over throughput optimization. | Approved architectural principle | Charter and requirements |
| ASM-010 | Estimated roadmap duration and named folder layout are planning suggestions, not architecture or contractual delivery commitments. | Governance | Documentation approval |

## 4. Initially identified unknowns and conflicts

### Scope and product semantics

- The terms **file transfer**, **delta sync**, **download**, and **synchronization** are used interchangeably. The authoritative direction and conflict semantics are not defined.
- It is unknown whether v1 handles one file, batches, directory trees, file metadata, sparse files, or symbolic links.
- Existing-destination behavior is undefined: overwrite, version, reject, prompt, or reconcile.
- The target user and deployment trust boundary are not explicit: a single user between owned hosts differs materially from a public or multi-user server.

### Architecture

- The source says the scheduler changes “chunk size” during a transfer. Changing CDC boundaries after a manifest is established would destabilize chunk identity, delta comparison, deduplication, and resume. The design must distinguish immutable **logical chunks** from optional adaptive **transport segments**, or choose another explicit model.
- “TCP send (N threads)” does not specify one multiplexed connection, one connection per worker, or another connection model. Framing, ordering, fairness, retry, and scheduler behavior depend on this decision.
- Roles conflict in the resume narrative: the server appears to hold the partial destination state, but the source also says the client reads the partial manifest on reconnect.
- Download-from-multiple-sources and rarest-chunk-first behavior appear as a USP, but peer discovery, availability exchange, trust, and multi-source consistency are absent from the architecture and milestones.
- The persistence technology, on-disk layout, locking model, atomicity guarantees, and multi-process access policy are undefined.
- The existing repository has no implementation files, so every proposed module path remains provisional.

### Security and data integrity

- Per-chunk AES-GCM does not by itself define peer authentication, authorization, metadata integrity, replay prevention, protocol downgrade prevention, or key distribution/rotation.
- AES-GCM nonce generation and uniqueness are unspecified and are correctness-critical.
- Cross-transfer deduplication and encryption interact: randomized encryption prevents ciphertext equality, while plaintext hash lookup can reveal content equality. The trusted party and allowed leakage must be decided.
- It is unknown whether the server is trusted with plaintext and keys, or should store only encrypted content.
- Paths, filenames, file sizes, chunk counts, decompressed-size limits, and resource quotas need protocol-level validation to prevent traversal and resource-exhaustion attacks.

### Algorithms and experimental policy

- Rabin CDC polynomial/window and minimum/target/maximum chunk sizes are not specified. These should be versioned manifest parameters and experimentally justified.
- The entropy estimator, sampling method, threshold, and choice between LZ4/Zstd are not specified. Entropy alone may not predict net benefit once headers, CPU, and transfer conditions are included.
- Application ACK gaps over TCP are not a direct packet-loss measurement. The scheduler's observable signals and estimator must be defined without claiming unavailable transport data.
- Dynamic thread-pool resizing is proposed, but the number of TCP connections, in-flight work limits, hysteresis, oscillation control, and safe fallback are unspecified.
- Retry ownership and idempotency are not defined; retrying after an ambiguous ACK can duplicate work unless chunk writes and acknowledgements have precise semantics.

### Validation and benchmark claims

- The headline “~40% bandwidth reduction,” the target “≤10% of file for a 5% change,” and the later “~95% saved” are not one consistent success criterion.
- “At least 2×” stable-network performance versus SCP and specific MB/s examples are unverified. SCP is not a clean sequential-transfer control because it includes SSH behavior and may use different encryption/compression settings.
- “Near zero CPU wasted” and “0 CPU wasted” are not measurable as written. The benchmark needs defined CPU metrics and a comparison policy.
- A same-machine test can measure pipeline overhead but not representative network throughput without a controlled topology and cache policy.
- Repetitions, confidence reporting, file corpus, cold/warm cache behavior, CPU limits, and pass/fail thresholds are not defined.

### Operations and engineering

- C++ standard, compiler/OS matrix, dependency versions/licences, package strategy, and supported build generators are not approved.
- Storage retention, chunk garbage collection, quota behavior, backup, and recovery are undefined.
- Protocol compatibility and migration rules are absent.
- Terminal dashboard library and behavior in non-interactive output are undefined; this is an implementation choice unless it affects compatibility or dependencies.

## 5. Initial questions requiring user decisions

The first seven decisions are blocking for an implementation-ready architecture. Suggested choices are explicitly recommendations, not assumptions.

1. **What is the exact v1 transfer semantic?** Choose one: upload/push to a server, download/pull from a server, both directional commands, one-way mirroring, or true bidirectional synchronization with conflict resolution.  
   **Recommendation for the five-week solo scope:** push/upload of one regular file, with a later pull milestone if desired.

2. **What filesystem scope must v1 support?** Single regular files only, multiple files/directories, or full metadata-preserving filesystem replication? Define required handling of existing destinations.  
   **Recommendation:** single regular file per transfer; atomically replace an explicitly selected destination only after full verification; exclude symlinks, directories, permissions, and xattrs from v1.

3. **What is the trust/deployment model?** Is this a single-user system between hosts the operator controls, a trusted shared server, or a multi-user/untrusted storage service?  
   This determines authentication, authorization, key management, metadata exposure, and whether “global” deduplication is permissible.

4. **Is raw TCP plus application-layer AES-GCM a fixed requirement or only a roadmap suggestion?** Alternatives include authenticated TLS for transport plus hashes for stored-chunk integrity, or an explicitly designed application cryptosystem.  
   **Recommendation:** do not approve the current AES-GCM concept until peer authentication, key exchange/storage, nonce uniqueness, and metadata integrity are defined.

5. **Should CDC chunks remain immutable for a manifest while the scheduler adapts only transport concurrency and/or sub-segmentation?**  
   **Recommendation:** yes. Version the CDC policy; never rechunk an active transfer in response to network changes.

6. **Which parallel transport model should be designed?** One framed/multiplexed TCP connection, multiple TCP connections, or leave the choice open for an ADR backed by a prototype?  
   **Recommendation:** approve the requirement for bounded parallel in-flight chunks, but select the connection model through an early ADR/prototype rather than declaring “N threads” as architecture.

7. **What persistent-store constraints apply?** Is an embedded database allowed, or must manifests/chunks use ordinary files only? Must multiple clients/transfers operate concurrently?  
   The answer governs crash consistency, indexes, locking, garbage collection, and recovery.

8. **Is multi-source/peer-to-peer download and rarest-chunk-first part of v1, a later milestone, or out of scope?**  
   **Recommendation:** later milestone; it introduces peer discovery and trust/protocol work not represented in the five-week plan.

9. **What platform is required?** Linux-only for v1, Linux plus Windows, or portable POSIX/Windows from the start? Which compilers are required?  
   **Recommendation:** Linux-first benchmark/runtime target with portability boundaries documented; expand only if portfolio goals require it.

10. **How should the roadmap's numeric claims be treated?** Hard acceptance thresholds, aspirational experiment targets, or examples to replace with measured baselines?  
    **Recommendation:** treat them as hypotheses; approve correctness/recovery gates separately and publish performance only from the reproducible benchmark suite.

11. **Is cross-transfer deduplication limited to one trusted user/namespace, or intended across users?**  
    **Recommendation:** one trusted namespace in v1; cross-user deduplication requires a separate privacy/security decision.

12. **Is the five-week schedule a hard deadline or simply an ordering proposal?** If hard, which features may be deferred to preserve correctness and a demonstrable end-to-end slice?

## 6. Approval gate and next step

No architecture in the source roadmap is accepted merely by appearing there. After the user answers the blocking questions and approves or modifies this structure, documentation should be generated in dependency order:

1. Approve charter, vocabulary, scope, and documentation governance.
2. Write requirements and flows without choosing unresolved implementation mechanisms.
3. Record the necessary ADRs.
4. Define architecture, domain/data models, wire/CLI contracts, and failure behavior.
5. Define security, observability, testing, benchmarks, delivery, and operations.
6. Build traceability and milestones from the approved artifacts.
7. Run contradiction and implementation-readiness reviews; do not declare READY while blocking decisions remain.

**Initial planning status (superseded by the completed corpus): NOT READY.**

**Critical gaps:** v1 semantics, trust/security model, transport/concurrency design, CDC/scheduler boundary, and persistence model.

**Open decisions:** the twelve questions above, with questions 1–7 blocking the core architecture.

**Architecture risks:** unstable chunk identity if scheduling changes CDC boundaries; incomplete security despite per-chunk encryption; ambiguous resume ownership; unsafe or privacy-leaking deduplication; protocol/retry inconsistency; invalid benchmark claims.

**Implementation blockers:** no approved scope, authoritative state model, wire protocol, recovery semantics, or security boundary.

**Recommended next step:** approve or amend the resolution package below. Once approved, it becomes the basis for the charter, requirements, and ADRs.

## 7. Proposed resolution package

> Status: **APPROVED WITH DIRECT-TRANSFER AMENDMENT — 2026-09-22**  
> Basis: The user requested multiple-file and parallel-transfer support, delegated the security, chunking/scheduling, connection, and persistence recommendations, and then approved direct ShareIt-style transfers without a permanent server.

### Plain-language model

Spoink v1 will let one device **send a batch of ordinary files directly** to another device on the same local network. There is no permanent central server. Both machines are controlled by the same person or trusted team. The sender and receiver prove their identities with certificates, then communicate through encrypted TLS connections. Files are divided into stable content-defined chunks so later transfers can reuse unchanged data. Several files and chunks may be processed concurrently, but concurrency is bounded to prevent resource exhaustion.

The receiving device records manifests and transfer state in SQLite and stores chunk bytes in a content-addressed filesystem store. Interrupted sessions resume from the receiver's durable record of verified chunks. Either device can be the receiver in a later transfer. Network adaptation may change how much work is in flight and how many data connections are active; it must never redefine the logical chunks in an active manifest.

### Proposed decisions

| Ref | Proposed v1 decision | Why this is recommended |
|---|---|---|
| PR-01 | V1 is **direct device-to-device send/receive** on a local network. A transfer session may contain multiple regular files. Either device may be the sender on a later transfer; automatic two-way synchronization and conflict resolution are later scope. | Provides ShareIt-style transfer without requiring a permanent server or introducing automatic merge rules. |
| PR-02 | Inputs may be multiple explicitly selected regular files. A directory may be used only as a selection root for recursive regular-file discovery. Relative paths are preserved. Symlinks, devices, sockets, sparse-file preservation, ACLs, xattrs, and permission replication are out of v1 scope. | Supports realistic multi-file batches while avoiding platform-specific filesystem semantics. |
| PR-03 | Each file is written to staging storage, verified, and then atomically finalized. The default rejects an existing destination; an explicit overwrite option permits atomic replacement. A batch may partially succeed, and the result reports status per file. | Prevents accidental data loss and avoids claiming impossible cross-file atomicity. |
| PR-04 | Trust model: **paired devices in one administrative domain**. The operator/team controls both endpoints. There is no public service, multi-tenant account system, or per-user ACL system in v1. | This is the safest scope for a direct-transfer prototype while still requiring real device authentication and secure transport. |
| PR-05 | Use **TLS 1.3 with mutual TLS (mTLS)** over TCP. Both peers present certificates. Do not implement custom per-chunk AES-GCM in v1. SHA-256 remains the content identifier/integrity check, while TLS supplies in-transit confidentiality and tamper protection. | mTLS solves peer authentication and transport encryption together. Custom AES-GCM would additionally require a correct key-distribution, nonce, replay, and metadata-authentication design. |
| PR-06 | Stored chunks are plaintext within the receiving device's trusted local store in v1. At-rest encryption is a separate future decision. Deduplication is local to each device. | TLS does not encrypt stored data. Stating this explicitly prevents a false security claim and avoids cross-user deduplication leakage. |
| PR-07 | CDC logical chunks are immutable for the lifetime of a versioned manifest. The scheduler may change connection concurrency, the in-flight byte/chunk window, retry timing, and work order, but not CDC boundaries. | If logical chunk boundaries changed with network conditions, hashes would stop matching and delta transfer, deduplication, and resume would become unreliable. |
| PR-08 | Use one mTLS control connection per direct transfer session and a bounded, dynamically sized pool of mTLS data connections. All data connections are bound to the paired sender identity and transfer session. Multiple files and chunks can progress concurrently across the pool. | Preserves the project’s adaptive/parallel-transfer goal while keeping commands, session state, and data transfer roles explicit. |
| PR-09 | The protocol provides idempotent chunk submission. A chunk is acknowledged only after its bytes and metadata pass validation and its durable state is committed. Duplicate submission of the same chunk for the same session is safe. | Makes retries and reconnects deterministic even when an acknowledgement is lost. |
| PR-10 | On every device that receives files, persist manifests, file/session state, and verified-chunk references in **SQLite**, using transactions and WAL mode. Store chunk payloads as content-addressed files, committed with temporary-file plus atomic-rename semantics. | SQLite provides transactions and recovery without operating a database server; the filesystem is better suited to potentially large immutable byte blobs. |
| PR-11 | Concurrent transfer sessions are supported. Concurrency is bounded by configuration. Per-session and per-destination coordination prevents conflicting writes; SQLite transactions protect metadata. | Directly supports the requested parallel file transfers without promising unlimited concurrency. |
| PR-12 | Each transfer has one sender and one receiver. Multi-source/swarm transfer and rarest-chunk-first scheduling are deferred beyond v1. | Direct transfer does not require a central server, but multi-source swarming would add peer discovery, availability exchange, and significantly more trust/protocol work. |
| PR-13 | Linux is the v1 runtime and benchmark target; GCC and Clang builds are intended. The language baseline remains C++17 unless an approved dependency or design later justifies a change. | Matches the source roadmap and its `tc netem` benchmark method while keeping the first release achievable. |
| PR-14 | Roadmap numbers are experimental hypotheses, not release promises. Correctness, integrity, safe recovery, and reproducibility are hard release gates; performance claims are published only after measurement. | Prevents unmeasured estimates from becoming misleading requirements or résumé claims. |
| PR-15 | The five-week roadmap is an ordering guide, not a guaranteed calendar commitment. Correctness and the end-to-end transfer slice take priority; adaptive scheduling and advanced deduplication may move later if evidence requires it. | Maintains a usable critical path without silently weakening correctness to meet an arbitrary date. |
| PR-16 | V1 connects by an explicitly supplied LAN address. Automatic discovery is deferred. First-time device pairing uses mutual certificate identities plus a user-compared short authentication string; later connections require the pinned peer identity. | Manual addressing keeps discovery out of the critical path, while verified pairing prevents silent trust of an unknown device. |

### What the delegated choices mean

- **Trusted devices** means the receiving device is allowed to see and store the file contents, and both endpoints are managed by the same person or team. If a receiver must not see plaintext, PR-04 through PR-06 must be redesigned.
- **mTLS** means both devices prove which stored device identity they possess. On first pairing, the users compare a short code shown on both screens; later connections must match the saved certificate identity. Certificate creation, pairing, and revocation will be documented operationally.
- **Immutable CDC chunks** means content-defined chunking creates the reusable identity map. The network scheduler moves those pieces more or less aggressively but does not redraw the map during transfer.
- **Control plus data connections** means one connection coordinates the session while a bounded pool carries chunk bytes. The pool can grow or shrink from measured conditions without corrupting manifest identity.
- **SQLite plus chunk files** means transactional metadata is kept in an embedded database, while large chunk payloads remain ordinary immutable files. This is not a separate database server and works with parallel sessions when writes are kept short and bounded.

### Remaining non-blocking details

The corpus will still need to define certificate provisioning, destination naming, CLI syntax, configuration defaults, CDC parameters, compression-routing thresholds, concurrency limits, retry budgets, retention/garbage collection, benchmark workloads, and exact dependency versions. These are configuration, experimental policy, operational detail, or later ADR content; they do not need to be guessed at the structure-approval stage.
