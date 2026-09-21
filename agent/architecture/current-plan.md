# Architecture Plan

- **Task ID:** `UNSPECIFIED` (`agent/task.md` is missing)
- **Status:** `HUMAN_DECISION_REQUIRED`

## Task interpretation

No implementable task can be interpreted from the repository because the required task contract, `agent/task.md`, does not exist. The only project document is an unapproved proposal for a system named DeltaSync. It explicitly says that its source roadmap is evidence rather than an approved set of requirements or architectural decisions.

If the intended task is to implement any material part of the proposed DeltaSync system, architectural work is required. The proposal involves public wire and CLI contracts, persistent manifests and recovery state, concurrency, cryptographic and trust boundaries, deduplication, and crash recovery. Which of those concerns belongs to the current task cannot be determined without a Task ID, scope, and acceptance criteria.

## Verified current-state findings

### Repository facts

- `agent/task.md` is absent, so there is no Task ID, requested change, or acceptance contract.
- No prior `agent/architecture/current-plan.md` existed when this assessment began.
- The repository has no tracked commits or tracked files. Its only pre-existing file is the untracked `docs/00_documentation_plan.md`.
- There are no production source files, tests, migrations, build files, runtime configuration files, project handbook, or accepted architecture documents to inspect.
- `docs/00_documentation_plan.md:3` marks itself **PROPOSAL — NOT YET APPROVED**.
- `docs/00_documentation_plan.md:6` says the roadmap's schedules, designs, benchmarks, and implementation notes are not automatically approved requirements or decisions.
- `docs/00_documentation_plan.md:180` also records that there is no implementation and all proposed module paths are provisional.
- `docs/00_documentation_plan.md:215` identifies seven unresolved decisions as blockers to an implementation-ready architecture.
- `docs/00_documentation_plan.md:264-274` concludes that documentation is not ready and that scope, state, wire protocol, recovery, and security boundaries remain unresolved.
- The roadmap referenced by the proposal exists at `E:\programs\c++\dift roadmap\dift-final-roadmap vibecode.html`, but it is project-description evidence, not repository policy or an accepted contract.

### Proposed ideas that are not verified requirements

The available documents discuss a C++ client/server file-transfer prototype with content-defined chunking, delta transfer, compression, TCP concurrency, resumability, a global chunk store, adaptive scheduling, and per-chunk AES-GCM. None of these is approved for the present task. Numeric performance claims are likewise unverified targets or examples.

## Decision required

### Exact decision

The Owner must first provide `agent/task.md` with:

1. a stable Task ID;
2. the requested outcome and explicit non-goals;
3. acceptance criteria;
4. the approved product scope relevant to that outcome; and
5. links or declarations identifying which proposal decisions, if any, have been accepted.

If the task authorizes implementation of the proposed DeltaSync system or one of its architecture-bearing slices, the human decision-maker must also resolve the applicable blocking choices already enumerated in `docs/00_documentation_plan.md:217-247`, especially:

- v1 transfer direction and semantics;
- filesystem scope and destination replacement behavior;
- deployment/trust model;
- transport security and authentication boundary;
- immutable logical CDC chunks versus adaptive transport segmentation;
- connection/concurrency model or authority to select it through an ADR/prototype; and
- persistence constraints and multi-client concurrency requirements.

### Available options and trade-offs

1. **Provide a narrowly scoped task.** Define one vertical slice or documentation/decision deliverable and answer only the architectural questions that affect it. This minimizes premature design, but later slices may require additional ADRs or contract evolution.
2. **Create an architecture-definition task for the complete v1 system.** Resolve the seven blocking choices before any Builder implementation. This produces coherent system-wide contracts, but requires more up-front product decisions and delays coding.
3. **Approve the roadmap as written.** This is not recommended: it would silently accept contradictory or incomplete behavior around adaptive chunking, resume ownership, security, persistence, retries, and multi-source transfer.

### Recommendation

Choose option 1 unless the immediate goal is explicitly to establish all v1 contracts. Start with a correctness-first, single-file transfer slice and treat broader features as separately approved work. The exact slice and its architectural boundaries must still be recorded in `agent/task.md`; this recommendation is not approval of a product scope.

### Work blocked by the decision

- Determining whether architecture is required for the actual task.
- Assigning stable component ownership and dependencies.
- Defining public or internal interfaces, protocol framing, state machines, and compatibility rules.
- Selecting or designing persistent state and migrations.
- Giving the Builder an exact implementation sequence or file list.
- Defining task-specific tests and declaring `READY_FOR_BUILD`.

## Proposed architecture

No architecture is approved in this plan. Adopting a concrete module topology now would convert an unapproved roadmap into product constraints without a task contract.

After the missing decisions are supplied, the next Architect pass should produce a plan for only the selected task. If the task is architecture-bearing, that plan should preserve these preliminary separation principles unless an approved ADR says otherwise:

- logical content chunks and their identities are distinct from transport scheduling or segmentation;
- protocol parsing/validation is separated from file, chunk-store, and reconstruction operations;
- durable transfer state has one authoritative owner and crash-consistent update rules;
- optimization policy cannot bypass integrity, authentication, authorization, resource bounds, or atomic publication;
- benchmark policy and telemetry do not become correctness dependencies.

These are boundaries to evaluate, not accepted interfaces.

## Components and ownership boundaries

Component ownership cannot be finalized. Candidate concerns visible in the proposal are:

- content chunking, hashing, and manifest creation;
- manifest comparison and transfer planning;
- transport protocol and bounded concurrency;
- compression and encryption transforms;
- persistent chunk and transfer-state storage;
- verified reconstruction and atomic destination publication;
- adaptive scheduling and telemetry.

The next plan must decide which of these are in scope and ensure each persistent datum and state transition has one authoritative owner. The roadmap's suggested folders are not binding.

## Interfaces, data flow, and persistent-state changes

No interface or persistent-state change is authorized. Required contracts depend on the missing task and decisions. In particular, the Architect must not freeze chunk identity, manifest format, wire framing, acknowledgement/idempotency semantics, on-disk layout, encryption metadata, or recovery checkpoints from roadmap prose alone.

## Exact implementation sequence

Builder implementation is blocked. The required sequence before build work is:

1. Owner creates `agent/task.md` with the information listed under **Decision required**.
2. Human decision-maker approves the relevant product/security/persistence choices or explicitly scopes them out.
3. Architect re-inspects the repository and writes a task-specific plan with verified interfaces, state ownership, files, compatibility behavior, and tests.
4. Builder implements only after this plan is replaced with `Status: READY_FOR_BUILD` (or an assessment with `Status: NOT_REQUIRED`).

## Files expected to change

- `agent/task.md` — must be created by the Owner/task authority; this Architect will not invent its contents.
- `agent/architecture/current-plan.md` — must be replaced by the Architect after the task and decisions exist.
- Production, test, migration, build, and configuration file changes — **cannot be identified yet**.

## Compatibility and migration considerations

There is no existing implementation or persisted format to migrate. Nevertheless, any first accepted manifest, wire protocol, chunk identity rule, or disk format will establish future compatibility constraints. The task-specific plan must define versioning and unknown-version rejection before those formats are implemented.

## Security and failure-mode considerations

The current proposal does not define a complete security boundary. Per-chunk AES-GCM alone does not decide peer authentication, authorization, key establishment/storage, nonce uniqueness, replay/downgrade protection, or metadata integrity. Global plaintext-hash deduplication may also disclose content equality across trust domains.

No network-facing or persistent implementation should be declared build-ready until the applicable plan defines:

- trusted principals and data exposure;
- path normalization and destination authorization;
- bounded sizes/counts, decompression limits, quotas, and concurrency;
- authenticated protocol negotiation and cryptographic lifecycle, if encryption is in scope;
- idempotent retry and acknowledgement behavior;
- atomic durable updates and crash recovery;
- integrity verification before final destination publication; and
- safe handling of corrupt state, interrupted writes, network loss, and process termination.

## Acceptance criteria

This blocking plan is resolved only when:

- `agent/task.md` exists and names a Task ID, scope, non-goals, and testable acceptance criteria;
- every architecture-affecting decision needed by that scope is approved or explicitly deferred out of scope;
- proposal statements are clearly classified as accepted requirement, assumption, experiment, or rejected idea;
- a subsequent Architect pass can unambiguously issue `READY_FOR_BUILD` or `NOT_REQUIRED`; and
- the Builder can implement without making product, security-boundary, persistent-format, or public-contract decisions.

## Required tests and verification

No task-specific test suite can be prescribed yet. Before the next Architect pass, verify:

- the Task ID and acceptance criteria are internally consistent;
- referenced decisions have an authoritative approval record; and
- the repository inventory has not gained relevant source, tests, migrations, configuration, or architecture documents that were omitted from review.

If implementation is later authorized, its plan must require proportionate unit, property/fuzz, protocol integration, crash-recovery, security-negative, compatibility, and end-to-end integrity tests for the components actually in scope. Benchmark targets must be validated separately from correctness gates.

## Explicit assumptions

- **Assumption A1:** The missing `agent/task.md` is an orchestration omission, not permission to infer the entire roadmap as the task.
- **Assumption A2:** `docs/00_documentation_plan.md` accurately describes its own approval state.
- **Assumption A3:** No out-of-repository roadmap statement is authoritative unless the Owner cites it in the task or an approved decision artifact.
- **Assumption A4:** The Builder must not begin production implementation from this blocking plan.

No project-product assumptions from `docs/00_documentation_plan.md` are promoted to facts here.

## Instructions and boundaries for the Builder

- Do not modify production code under this plan.
- Do not create public interfaces, wire/disk formats, schemas, cryptographic behavior, or concurrency models from the roadmap alone.
- Do not treat proposed numeric benchmarks as acceptance criteria.
- Return control to the Owner for the missing task contract and required human decisions.
- Begin implementation only after this artifact is replaced by a plan marked `READY_FOR_BUILD`, or by an assessment marked `NOT_REQUIRED` for the same Task ID.
