# Internal Component Contracts

> Status: Approved behavior; C++ names are recommended targets, not required ABI.

## Common conventions

- Fallible cross-module operations return a project `Result<T, Error>` (or semantically equivalent type). Exceptions MUST NOT cross module boundaries; third-party exceptions are translated.
- `Error` contains stable code, category, retry class, safe user message key, correlation ID, and a nested local diagnostic that is never sent raw to peers.
- Byte views carry pointer/span plus explicit length. Ownership is explicit; asynchronous work owns or shares immutable storage for its full lifetime.
- All sizes/offsets use checked 64-bit arithmetic. Conversion to `size_t`, SQLite signed integer, or library-specific types is checked.
- Time uses monotonic clocks for durations/deadlines and UTC system time only for records/display.
- Cancellation is explicit through a token/context; it does not masquerade as network failure.

## IC-001 — Identity store

```text
load_or_create_identity(options) -> Result<DeviceIdentity>
list_peers() -> Result<vector<TrustedPeer>>
pin_peer(PairingEvidence) -> Result<TrustedPeer>
revoke_peer(PeerId) -> Result<void>
verify_peer_certificate(Certificate, mode) -> Result<PeerId>
```

`pin_peer` requires validated SAS evidence from the current pairing connection. Private key access is limited to TLS configuration; callers never receive printable key bytes.

## IC-002 — Path selector

```text
select_files(inputs, optional_root, policy) -> Result<SelectedFileSet>
validate_manifest_path(relative_utf8) -> Result<NormalizedPath>
resolve_destination(root_handle, NormalizedPath) -> Result<ContainedPath>
```

Selection does not follow symlinks. Destination resolution uses directory-handle/openat-style containment on Linux where practical; string-prefix checks alone are forbidden.

## IC-003 — Chunker

```text
chunk_stream(ReadSource, CdcProfile, ChunkVisitor) -> Result<FileDigest>
```

Visitor receives ordered `{ordinal, offset, length, sha256, bytes-or-source-locator}`. The chunker is deterministic, streaming, and bounded; it does not know network conditions. A source stability guard verifies identity/size/mtime and content hashes before sending.

## IC-004 — Manifest codec

```text
build_manifest(SelectedFiles, CdcProfile) -> Result<CanonicalManifest>
decode_and_validate_manifest(bytes, Limits) -> Result<CanonicalManifest>
```

`CanonicalManifest` owns exact canonical CBOR and digest. Decoding rejects non-deterministic encodings rather than normalizing and accepting a different byte representation.

## IC-005 — Compression router

```text
encode_chunk(PlainChunk, CompressionPolicy, PeerCapabilities) -> Result<EncodedChunk>
decode_chunk(EncodedChunkView, expected_plain_length, Limits) -> Result<PlainChunk>
```

Decode must enforce output bound before/during library calls. Encode may return raw. Compression failure can fall back to raw only before submission and only when raw is negotiated.

## IC-006 — Protocol codec

```text
encode_frame(TypedMessage, optional_payload, NegotiatedLimits) -> Result<Buffers>
feed_bytes(ByteView) -> Result<vector<TypedFrame>>
validate_message(TypedFrame, ProtocolState) -> Result<TypedMessage>
```

Framing validates lengths before buffering. Schema validation and state-machine validation are separate. No direct use of network CBOR maps is allowed in domain/storage code.

## IC-007 — Session controller

```text
on_message(PeerId, ConnectionId, TypedMessage) -> vector<Effect>
on_effect_result(EffectId, Result) -> vector<Effect>
snapshot() -> SessionSnapshot
```

The controller is a deterministic state machine. Effects represent I/O, persistence, prompts, timers, and sends; callbacks are serialized per session. Tests can drive it without sockets/filesystems.

## IC-008 — Chunk store

```text
inventory(hashes) -> Result<HashStateMap>
commit_verified_chunk(SessionId, Hash, PlainLength, TempBlob) -> Result<CommitStatus>
open_verified_chunk(Hash, LeaseContext) -> Result<LeasedReader>
release_lease(LeaseId) -> Result<void>
```

`CommitStatus` is `STORED` or `ALREADY_PRESENT`. Success has durable ACK semantics. The store accepts only a temp blob already verified by the caller but rechecks metadata and canonical-path invariants.

## IC-009 — Metadata repository

Repositories expose domain operations, not raw SQL to other modules:

```text
accept_manifest(peer, destination, manifest, file_decisions) -> Result<Session>
record_chunk_verified(session, hash) -> Result<void>
load_resume(peer, transfer_id, manifest_digest) -> Result<ResumeState>
transition_session(id, expected_state, new_state, reason) -> Result<void>
```

Each method documents transaction scope. State transitions use expected prior state to prevent races.

## IC-010 — Scheduler

```text
observe(SchedulerEvent) -> void
evaluate(MonotonicTime) -> SchedulerDecision
next_work(ResourceSnapshot) -> optional<ChunkTask>
```

Policy is pure/deterministic for the same ordered events, clock, and config. Connection management executes decisions but clamps them again to negotiated/device limits.

## IC-011 — Reconstructor

```text
reconstruct(SessionFile, ChunkReaderFactory, DestinationPolicy) -> Result<VerifiedStagingFile>
finalize(VerifiedStagingFile, DestinationPolicy) -> Result<FinalFile>
```

The type system or explicit state token prevents calling `finalize` on unverified staging data. Existing destinations remain unchanged until final atomic operation.

## IC-012 — Telemetry sink

```text
emit(TelemetryEvent) -> EmitResult
finish_transfer(SessionId) -> Result<TransferReport>
```

Correctness paths do not depend on low-priority event delivery. Security/audit and terminal-result events use a reserved bounded channel and surface failure if they cannot be persisted where required.
