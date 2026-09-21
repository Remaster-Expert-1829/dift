# Requirements

> Status: Approved  
> Scope authority: [Project charter](../00_governance/00_01_project_charter.md)

## Functional requirements

### Identity and trust

#### FR-001 — Local device identity

- **Actors:** Operator, local peer application.
- **Input:** `init` request and optional display name.
- **Output:** Locally stored private key, self-signed device certificate, stable peer fingerprint, and non-secret device metadata.
- **Behavior:** The application MUST generate identity material using the approved TLS library and cryptographically secure randomness. It MUST refuse insecure private-key permissions.
- **Constraints:** Private keys never leave the device through DeltaSync. Re-initialization MUST require explicit confirmation because it invalidates existing pairings.
- **Acceptance:** A fresh device obtains a stable fingerprint; a second normal initialization does not replace it; private key material is not printed or logged.

#### FR-002 — First-time pairing

- **Actors:** Sender-side and receiver-side operators and peer applications.
- **Input:** Explicit address, both device certificates, pairing mode, and user confirmation of a shared short authentication string (SAS).
- **Output:** A pinned trusted-peer record on both devices or no record.
- **Behavior:** Both applications MUST show the same SAS derived from the authenticated TLS session. Trust MUST be saved only after confirmation on both endpoints. Rejecting, timing out, or obtaining different confirmations MUST leave no trusted pairing.
- **Constraints:** Pairing mode is explicit and temporary; it MUST NOT accept file-transfer commands before confirmation.
- **Acceptance:** Confirmed endpoints reconnect using pinned identities; a man-in-the-middle producing distinct TLS sessions yields different SAS values; rejection saves no peer.

#### FR-003 — Paired-peer enforcement

- **Actors:** Peer application.
- **Input:** Incoming normal connection.
- **Output:** Accepted authenticated session or stable authentication error.
- **Behavior:** Normal transfer connections MUST require a non-revoked pinned peer certificate. Certificate replacement is rejected and requires deliberate re-pairing.
- **Acceptance:** Unknown, revoked, expired under policy, or changed identities cannot offer or receive files.

### File selection and session negotiation

#### FR-004 — Receiver mode

- **Actors:** Receiving operator.
- **Input:** Destination root, listen address, configuration, optional allowed peer.
- **Output:** A listening receiver with displayed reachable address and identity.
- **Behavior:** The receiver MUST validate its destination and storage paths before accepting sessions and MUST expose a clear stop action.
- **Acceptance:** Invalid/unwritable destinations fail before listening; normal termination stops new work and handles active sessions per `FR-027`.

#### FR-005 — Multiple-file selection

- **Actors:** Sending operator.
- **Input:** One or more regular files and/or explicitly selected directory roots.
- **Output:** A deterministic transfer file set with normalized relative paths.
- **Behavior:** Directory inputs are recursively enumerated. Unsupported types are rejected or skipped only with an explicit per-path result; they are never followed silently.
- **Constraints:** Symlinks MUST NOT be followed in v1. Duplicate normalized destination paths are errors.
- **Acceptance:** A mixed input containing three regular files offers exactly those files; unsupported and duplicate paths produce deterministic errors before content transfer.

#### FR-006 — Session offer and approval

- **Actors:** Sender, receiver, receiving operator.
- **Input:** Authenticated peer, file manifest summary, total logical size, file count, destination-relative paths, overwrite intent.
- **Output:** Accepted file subset and transfer ID, or rejection with reason.
- **Behavior:** The receiver MUST display the peer and proposed paths/sizes and require approval unless a documented allow rule explicitly enables unattended receipt. V1 defaults to interactive approval.
- **Acceptance:** Rejected files send no content; acceptance is bound to the exact manifest identity and peer.

#### FR-007 — Destination policy

- **Actors:** Receiver.
- **Input:** Accepted file paths and explicit overwrite flag.
- **Output:** Reserved staging/final paths or rejection.
- **Behavior:** Existing destinations are rejected by default. Explicit overwrite permits replacement only after successful verification. Paths MUST remain within the selected destination root.
- **Acceptance:** Traversal and absolute paths are rejected; failed replacement preserves the prior destination.

### Chunking, delta, and compression

#### FR-008 — Deterministic content-defined chunking

- **Actors:** Sender chunker.
- **Input:** Regular-file bytes and a versioned CDC profile.
- **Output:** Ordered logical chunks covering every byte exactly once.
- **Behavior:** Identical bytes with the same profile MUST produce identical boundaries and hashes. The first offset is zero, chunks are contiguous, and total length equals file length.
- **Acceptance:** Golden vectors and property tests prove determinism, coverage, configured min/max behavior, and forced final/max boundaries.

#### FR-009 — Manifest construction

- **Actors:** Sender manifest builder.
- **Input:** File metadata, ordered chunks, SHA-256 chunk digests, whole-file SHA-256, and profile/version identifiers.
- **Output:** Canonical transfer manifest.
- **Behavior:** Manifest identity MUST cover all fields that affect reconstructed bytes or destination paths. The manifest is immutable after the receiver accepts it.
- **Acceptance:** Changing path, size, order, digest, or CDC profile changes the canonical manifest digest.

#### FR-010 — Receiver-side delta selection

- **Actors:** Receiver store and sender.
- **Input:** Accepted manifest and receiver chunk inventory.
- **Output:** Set of missing chunk hashes.
- **Behavior:** The receiver MUST request only chunks not already present and verified in its local content store. The sender MUST NOT treat a Bloom filter or cache hint as authoritative proof of presence.
- **Acceptance:** An unchanged re-transfer sends zero chunk payload bytes; a partially changed transfer sends precisely the missing logical chunk payloads, subject to compression and framing overhead.

#### FR-011 — Compression routing

- **Actors:** Sender compression component.
- **Input:** Logical chunk, configured mode, entropy/benefit policy, supported-codec negotiation.
- **Output:** Original bytes or a losslessly compressed representation plus declared codec and original length.
- **Behavior:** `off`, `lz4`, `zstd`, and `auto` modes MUST be supported if both codecs are built. `auto` MUST skip chunks predicted not to produce net benefit. A compressed representation MUST be used only when it is smaller after transfer framing.
- **Constraints:** Thresholds and codec levels are configurable/experimental; receivers validate declared and decoded sizes.
- **Acceptance:** Round trips reproduce exact bytes; incompressible input is sent uncompressed under the approved auto policy; malformed compressed input is rejected safely.

### Parallel transfer and scheduling

#### FR-012 — Bounded parallel transfer

- **Actors:** Sender and receiver transfer engines.
- **Input:** Accepted session, missing chunks, configured connection/task/in-flight limits.
- **Output:** Concurrent, bounded chunk delivery.
- **Behavior:** Multiple files and chunks MAY progress concurrently. The system MUST enforce limits across each session and device.
- **Acceptance:** Under stress, observed connections, queued tasks, memory, and in-flight bytes do not exceed configured/derived limits.

#### FR-013 — Adaptive scheduling

- **Actors:** Scheduler.
- **Input:** Application RTT probes, acknowledged useful bytes, chunk completion latency, timeout/error rate, configured bounds.
- **Output:** Active data-connection target, in-flight-byte target, retry delay, and work order.
- **Behavior:** Adaptation MUST NOT change an accepted manifest or logical CDC boundaries. It MUST support a static fallback and expose every decision with its observed inputs.
- **Acceptance:** Deterministic simulation tests keep outputs within bounds, avoid rapid oscillation under fixed inputs, reduce load after repeated timeouts, and recover gradually after stability.

### Integrity, persistence, and resume

#### FR-014 — Chunk validation

- **Actors:** Receiver.
- **Input:** Chunk frame, declared hash/length/codec, decoded plaintext.
- **Output:** Durable verified chunk or error/NACK.
- **Behavior:** The receiver MUST enforce size limits, decode safely, and compare the plaintext length and SHA-256 digest before committing.
- **Acceptance:** Bit flips, wrong hashes, decompression overflow, truncated frames, and excess data never become verified chunks.

#### FR-015 — Durable chunk acknowledgement

- **Actors:** Receiver.
- **Input:** Validated chunk.
- **Output:** ACK or failure.
- **Behavior:** ACK means the chunk payload is atomically installed in the content store and the session receipt is committed. A crash after ACK MUST NOT require that chunk payload again unless storage corruption is detected.
- **Acceptance:** Fault-injection tests at every commit step produce either no ACK/no receipt or a recoverable verified chunk/receipt.

#### FR-016 — Reconstruction and whole-file verification

- **Actors:** Receiver reconstruction component.
- **Input:** Ordered manifest and verified chunks.
- **Output:** Staged file or integrity error.
- **Behavior:** Reconstruct in manifest order, enforce offsets/lengths, and verify size and whole-file SHA-256 before finalization.
- **Acceptance:** Missing, reordered, wrong, or corrupt chunks prevent completion; correct data produces the expected digest.

#### FR-017 — Atomic per-file finalization

- **Actors:** Receiver.
- **Input:** Verified staged file and destination policy.
- **Output:** Completed destination file.
- **Behavior:** Finalization MUST use same-filesystem atomic replacement where supported. It MUST preserve an existing destination until the new staged file is verified.
- **Acceptance:** Process termination before rename leaves no partial final file; after successful rename the final file is complete.

#### FR-018 — Interrupted-session resume

- **Actors:** Paired sender and receiver.
- **Input:** Transfer ID, peer identity, immutable manifest digest, receiver's durable session state.
- **Output:** Resumed session or explicit incompatibility/restart response.
- **Behavior:** Resume MUST authenticate the same peer and match the accepted manifest. The receiver returns missing chunks from durable state; already verified chunks are not retransmitted.
- **Acceptance:** Network loss and process termination at multiple transfer stages resume correctly; changed inputs require a new session.

#### FR-019 — Idempotent chunk submission

- **Actors:** Sender and receiver.
- **Input:** A duplicate chunk submission for the same session/hash.
- **Output:** The same success state without duplicated content/reference state.
- **Acceptance:** Repeating a validated frame after a lost ACK does not corrupt refcounts, duplicate blobs, or change reconstructed output.

#### FR-020 — Session status and terminal results

- **Actors:** Both operators.
- **Input:** Active or resumed session.
- **Output:** Per-file state, overall state, errors, bytes sent/avoided, and resumability.
- **Behavior:** Batch success requires every accepted file to complete. Partial completion is reported, not collapsed into success.
- **Acceptance:** Exit status and final report distinguish completed, partially completed, paused/resumable, rejected, cancelled, and failed outcomes.

### Deduplication, telemetry, and maintenance

#### FR-021 — Local cross-transfer deduplication

- **Actors:** Receiver chunk store.
- **Input:** Verified plaintext chunk hash.
- **Output:** Reused local chunk or newly stored chunk.
- **Behavior:** Identical verified chunks share one payload within a device. Deduplication MUST NOT cross devices or untrusted namespaces in v1.
- **Acceptance:** Two manifests referencing the same hash use one payload; deleting one manifest does not delete a chunk still referenced by another.

#### FR-022 — Live progress

- **Actors:** Operators.
- **Input:** Transfer telemetry.
- **Output:** Machine-readable and human-readable progress.
- **Behavior:** Show useful bytes, wire bytes, completion, current throughput, retry count, active connections, and per-file status. Interactive rendering MUST degrade to line-oriented output when stdout is not a terminal.
- **Acceptance:** Piped output contains parseable non-animated records and no control-sequence noise.

#### FR-023 — Decision telemetry

- **Actors:** Operator/developer.
- **Input:** Compression and scheduler decisions.
- **Output:** Structured events with decision, reason category, and non-sensitive inputs.
- **Acceptance:** A benchmark can reconstruct when/why concurrency and compression choices changed without access to file contents.

#### FR-024 — Transfer summary

- **Actors:** Operator and benchmark tooling.
- **Output:** Stable summary schema including logical bytes, payload bytes, protocol bytes where measurable, reused bytes, compressed bytes, duration, retries, and verification result.
- **Acceptance:** Repeated runs emit the same field names/units and explicitly mark unavailable measurements.

#### FR-025 — Peer listing and revocation

- **Actors:** Operator.
- **Input:** `peers list` or `peers revoke`.
- **Output:** Non-secret peer details or revoked record.
- **Behavior:** Revocation blocks new and resumed connections for that peer without deleting unrelated files.
- **Acceptance:** A revoked peer cannot authenticate normally; private keys are never displayed.

#### FR-026 — Store inspection and garbage collection

- **Actors:** Operator.
- **Input:** Inspect or GC request, retention policy, dry-run flag.
- **Output:** Store health/usage report and safely reclaimed unreferenced chunks.
- **Behavior:** GC MUST not delete chunks referenced by active, resumable, or retained completed manifests and MUST offer dry-run.
- **Acceptance:** Concurrent-reader and crash tests preserve referenced chunks; dry-run changes nothing.

#### FR-027 — Graceful stop and cancellation

- **Actors:** Operator.
- **Input:** Stop receiver or cancel session.
- **Output:** Stopped/paused state.
- **Behavior:** Stop accepting new sessions, stop scheduling new chunk work, finish or abort in-progress writes safely, persist recoverable state, and close connections. Forced termination remains recoverable through startup reconciliation.
- **Acceptance:** Graceful and forced termination never expose partial final files and leave a valid resume or explicit failed state.

## Non-functional requirements

| ID | Category | Requirement and measurable acceptance |
|---|---|---|
| NFR-001 | Correctness | No completed file may differ from its accepted manifest. End-to-end, property, and corruption tests MUST pass with zero undetected mismatches. |
| NFR-002 | Reliability | Acknowledged chunks survive process restart under the documented durable-store model. Crash-point tests MUST validate every write/commit boundary. |
| NFR-003 | Performance | The pipeline MUST avoid unbounded serialization between hashing, compression, I/O, and transfer. Numeric speed targets remain experimental and are not release gates until baselined. |
| NFR-004 | Security | Normal sessions require TLS 1.3 and pinned non-revoked device identities. Plain TCP fallback is forbidden. |
| NFR-005 | Secrets | Private keys and session capabilities MUST use OS CSPRNGs, restrictive permissions, zero unnecessary logging, and bounded lifetimes. |
| NFR-006 | Resource safety | Every externally influenced length, count, queue, connection pool, decompression output, path, and storage allocation MUST have a validated limit. |
| NFR-007 | Stability | Adaptive control output remains within configured bounds and provides static fallback. Scheduler failure cannot alter data correctness. |
| NFR-008 | Path safety | All received relative paths are normalized and contained below the chosen destination root; traversal and ambiguous encodings are rejected. |
| NFR-009 | Interoperability | A manifest/protocol version and CDC profile fully determine parsing and chunk boundaries. Unsupported major versions fail explicitly. |
| NFR-010 | Crash consistency | Startup reconciliation deterministically handles temporary blobs, staging files, DB rows, and final files after interruption. |
| NFR-011 | Data lifecycle | Retention and GC are explicit, inspectable, and conservative; inability to prove a chunk unreferenced prevents deletion. |
| NFR-012 | Maintainability | Core domain, protocol, persistence, security, and policy code are separated behind documented interfaces and compile with warnings-as-errors in project code. |
| NFR-013 | Observability | Logs and metrics contain correlation IDs and stable units while excluding file contents, private keys, session secrets, and full sensitive paths by default. |
| NFR-014 | Benchmark integrity | Published comparisons retain raw results, environment, versions, repetitions, cache state, and command lines; estimates are labeled as estimates. |
| NFR-015 | Accessibility | CLI errors use plain language, stable exit codes, and non-color equivalents; color and animation can be disabled. |
| NFR-016 | Portability | V1 MUST run on the documented Linux baseline. Platform-specific logic is isolated; other platforms are not claimed until tested. |
| NFR-017 | Availability | No always-on external service is required. A transfer requires only the two devices to be online and mutually reachable. |
| NFR-018 | Cost | V1 MUST use no mandatory paid service. Third-party library licenses must permit repository distribution and intended use. |

## Requirement classification notes

- CDC sizes, rolling-fingerprint parameters, compression thresholds/levels, scheduler windows, retry budgets, and resource limits are `EXP-*` or `CFG-*`, not product requirements.
- TLS, pinned identities, immutable manifest chunks, durable ACK meaning, and atomic finalization are architectural constraints because relaxing them changes correctness or trust.
- Concrete class names, thread primitives, terminal UI library, and source file layout are implementation details.
