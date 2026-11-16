# Architecture Plan

- Task ID: M2-001
- Status: READY_FOR_BUILD
- Branch assessed: main (work branch: feature/m2-001-secure-transfer)
- Baseline inspected: fcb43f6 (M2 task preparation on main; parent dbd333b merged M1-001)

## Task interpretation

M2-001 delivers "Secure direct-transfer vertical slice": pair two devices and transfer one regular file over a single TLS 1.3 control/data path with durable ACK and atomic completion. This introduces device identity (ARCH-01), TLS mutual authentication, pairing protocol (ADR-003, spoink-pair/1 ALPN), wire protocol framing (ADR-007, 32-byte fixed prefix), protocol state machine (HELLO through SESSION_RESULT), and network I/O — all new architecture domains not present in M0 or M1.

M2 scope is one regular file, one data connection, no compression (raw codec only), no resume, no delta/dedup, no multi-file batch. Those belong to M3/M4.

Two critical TBDs are resolved in this plan: TBD-SEC-KEY-001 (device key algorithm) and TBD-LIMITS-001 M2-required subset (parser, frame, handshake, and connection ceilings).

## Verified current state

- agent/active-task.md points to agent/tasks/M2-001.md with status READY_FOR_ARCHITECT_ASSESSMENT.
- main is at fcb43f6. Working tree is clean except for one unrelated untracked garbage file.
- M1-001 is merged at dbd333b (PR #4). 62/62 tests pass; ASan/UBSan clean; clang-tidy clean.
- M1 delivered: Rabin CDC with frozen rabin-v1 profile (polynomial=0x3da3358b4dc173, window=32, min=1024, target=16384, max=65536), SHA-256, canonical schema-1 CBOR manifests, versioned SQLite MetadataStore, durable content-addressed blob store, byte-identical reconstruction, reconcile-after-failure, reference-counted GC, and offline integration harness.
- Pinned dependencies: OpenSSL 3.6.0, SQLite 3.51.2, Catch2 3.11.0, utf8proc 2.11.3, vcpkg baseline 74e6536215718009aae747d86d84b78376bf9e09.
- M0 support headers provide Result/Error, checked arithmetic, SHA-256 digest type, strong PeerId/TransferId/RequestId types, clock, and RAII utilities (include/spoink/support/).
- The M1 Architect plan in this file is historical. This plan replaces it.
- Schema 1 (schema/001_initial.sql) already includes trusted_peers, transfer_sessions, transfer_files, chunk_catalog, file_chunk_refs, session_chunks, and chunk_leases — M2 uses the existing schema without migration.

## TBD-SEC-KEY-001 resolution

### Selected algorithm: Ed25519

**Exact specification:** Ed25519 (RFC 8032) via OpenSSL 3.6.0 EVP_PKEY_ED25519.

**Key parameters:**
- Key type: Ed25519 (OID 1.3.101.112)
- Public key size: 32 bytes
- Private key size: 32 bytes (64 bytes in expanded form)
- Signature size: 64 bytes
- Security level: 128-bit equivalent

**Evidence and rationale:**

1. **OpenSSL 3.6.0 support:** Full Ed25519 support for key generation (`EVP_PKEY_keygen` with `EVP_PKEY_ED25519`), self-signed X.509 certificate creation, and TLS 1.3 certificate authentication via the `ed25519` signature algorithm. Available since OpenSSL 1.1.1 (2018); mature in 3.x.

2. **TLS 1.3 compatibility:** Ed25519 is a first-class TLS 1.3 signature algorithm (RFC 8446 §4.2.3, SignatureScheme ed25519 = 0x0807). Both client and server certificate authentication are supported.

3. **Security strength:** 128-bit security level, meeting current best practice for new systems. Equivalent to ECDSA P-256 or RSA-3072. No known practical attacks.

4. **Deterministic signatures:** Ed25519 signatures are deterministic (no per-signature CSPRNG dependency), reducing failure modes during operation. CSPRNG is required only at key generation time.

5. **Small peer ID overhead:** The SPKI DER for Ed25519 is compact (44 bytes), producing a well-defined SHA-256 peer fingerprint.

6. **Precedent:** Used in SSH (RFC 8709), Signal, WireGuard, and TLS 1.3 implementations.

**Rejected alternatives:**
- ECDSA P-256: equivalent security but requires CSPRNG per signature, larger signatures (≈72 bytes DER), and more complex key handling.
- RSA-2048/3072: much larger keys (256–384 bytes) and signatures, slower operations, no advantage for self-signed peer identity.
- Ed448: 224-bit security is excessive for this application, larger keys/signatures, slower.
- Algorithm negotiation: rejected for M2. Both peers are under project control; a single algorithm eliminates downgrade attack surface. Future algorithm agility (e.g., post-quantum) would require a protocol minor extension or new major version.

**Enforcement rules:**
- `spoink init` generates only Ed25519 keys via OpenSSL EVP_PKEY_ED25519.
- Private key file is written with mode 0600 (owner read/write only); the Builder must verify and reject insecure permissions on load.
- On identity load, verify the key type is Ed25519 and the SPKI length/structure is valid. Reject any other key type with a clear error.
- TLS context restricts `SignatureAlgorithms` to ed25519 only for both client and server certificate verification.
- Self-signed certificate: X.509 v3, subject CN = device display name, validity = 10 years (3650 days), key usage = digitalSignature. The certificate is informational for display; trust is based on SPKI pin, not certificate chain validation.

### Binding decision

This is a binding M2 architectural decision. The Builder must implement Ed25519 key generation and TLS configuration exactly as specified. Any change to the selected algorithm requires Architect approval through an ADR amendment.

## TBD-LIMITS-001 (M2 subset) resolution

These are protocol-major-1 safety ceilings. They are absolute compiled constants. Operational defaults may be equal or lower. M3+ may widen per-session file count but the frame/parser ceilings become the protocol-major-1 compatibility contract.

### Frame and parser ceilings

| Parameter | Absolute ceiling | Default | Rationale |
|---|---|---|---|
| Frame prefix | 32 bytes (fixed) | 32 bytes | ADR-007 wire contract; not configurable |
| Max header bytes | 8,192 (8 KiB) | 8,192 | Largest CBOR headers are HELLO/SESSION_OFFER with limits/file summaries. 8 KiB accommodates all defined message types with generous margin. Validated: HELLO with full limits map + 10,000-entry file summary would require ~400 KiB, but that is SESSION_OFFER with file summaries split from the header. Individual headers stay under 2 KiB. |
| Max payload bytes | 131,072 (128 KiB) | 131,072 | Must accommodate rabin-v1 max_bytes=65,536 (raw codec). 128 KiB provides 2× headroom for potential future CDC profiles within protocol major 1 and for manifest parts. Compressed payloads are always ≤ raw size by protocol rule. |
| Max manifest bytes | 16,777,216 (16 MiB) | 16,777,216 | Matches M1 offline limit (Limits::kMaxManifestBytes). Paged MANIFEST_PART frames allow this to be transferred in 128 KiB parts. |
| Max path bytes | 4,096 | 4,096 | Matches M1 offline limit (Limits::kMaxPathBytes). Linux PATH_MAX. |

**rabin-v1 compatibility confirmed:** max_bytes=65,536 < max_payload_bytes=131,072. One chunk fits one frame.

### Session and connection ceilings

| Parameter | Absolute ceiling | M2 default | Rationale |
|---|---|---|---|
| Max files per session | 10,000 | 1 (M2 single-file) | M1 kMaxFiles. M2 enforces 1; M3 raises the operational default. |
| Max chunk references per session | 1,000,000 | 1,000,000 | M1 kMaxChunkReferences. With rabin-v1 min_bytes=1024 and max file size 256 GiB, theoretical max is ~268M; 1M is a practical safety ceiling. |
| Max data connections per session | 8 | 1 (M2 single data connection) | ADR-005 bounded pool. M2 uses exactly one; M4 may raise operational default. |
| Max concurrent sessions per device | 4 | 1 (M2 single session) | Resource safety. M3+ may raise. |
| Max concurrent handshakes | 8 | 4 | Limit resource consumption from unauthenticated connections. 8 absolute allows burst during reconnection. |

### Timeout ceilings

| Parameter | Absolute ceiling | Default | Rationale |
|---|---|---|---|
| Handshake timeout | 30,000 ms | 10,000 ms | TLS 1.3 handshake + certificate exchange + HELLO on a LAN. 10s default is generous for LAN; 30s ceiling for slow/loaded devices. |
| Connection idle timeout | 300,000 ms (5 min) | 60,000 ms | Detect dead connections. PING/PONG keepalive runs within this window. |
| Operation timeout | 300,000 ms (5 min) | 60,000 ms | Maximum time for a single protocol operation (e.g., waiting for SESSION_ACCEPT after offer). |
| Pairing attempt timeout | 120,000 ms (2 min) | 60,000 ms | Human SAS comparison time. One attempt per listener. |

### Negotiation rules

- HELLO exchanges include each peer's `limits` map with their operational values.
- The control receiver intersects by taking the minimum of each corresponding limit.
- No peer-advertised value above the local absolute ceiling is accepted; such values are clamped to the ceiling before intersection.
- Negotiated limits are immutable for the session lifetime.

### Binding decision

These ceilings become the protocol-major-1 compatibility contract. Frame header/payload ceilings may not be reduced in a future minor version. Session/connection/timeout values are operational and may be adjusted per minor version or configuration within the absolute ceilings.

## Proposed architecture and ownership table

| Boundary | M2 owner | Component(s) | Inward contract | Depends on (M1) | Must not own |
|---|---|---|---|---|---|
| Identity and key management | src/identity | ARCH-01 | IC-001 | — | File authorization, manifests, protocol state |
| Pairing protocol | src/pairing | ARCH-01 | API-001 (pairing) | IC-001 (identity store) | Normal transfer, session state |
| TLS and connection management | src/transport | ARCH-06 | IC-006 (framing) | IC-001 (TLS config) | Session decisions, store mutations |
| Wire protocol framing | src/protocol | ARCH-06 | IC-006 | — | Domain state, persistence |
| Protocol state machine | src/session | ARCH-02 | IC-007 | — | Raw socket I/O, crypto |
| Send orchestration | src/cli + session | ARCH-02/03/10 | IC-002/003/004 | M1 selection, chunking, manifest | Receiver decisions |
| Receive orchestration | src/cli + session | ARCH-02/07/10 | IC-008/009/011 | M1 store, reconstruct, metadata | Sender policy |
| Durable chunk commit + ACK | src/store (M1) | ARCH-07 | IC-008/009 | M1 blob store, MetadataStore | Protocol framing, network I/O |
| Reconstruction + finalization | src/reconstruct (M1) | ARCH-07 | IC-011 | M1 reconstruction | Peer trust, scheduling |

### Dependency direction

```text
CLI / transport adapters
         ↓
session orchestration (send/receive)
         ↓
protocol state machine (IC-007, pure/deterministic)
         ↓
protocol codec (IC-006, framing/CBOR)
         ↓
identity / pairing / trust (IC-001, API-001)
         ↓
M1 offline engine (selection, chunking, manifest, store, reconstruct)
         ↓
support (Result, types, checked_math, clock, RAII)
```

OpenSSL, SQLite, and utf8proc are wrapped at their boundaries. Domain types and protocol state machine do not depend on transport or persistence implementation.

## Format, protocol, and data-flow boundaries

1. **Identity creation flow:** `spoink init` → generate Ed25519 key pair via OpenSSL EVP → write private key PEM to `<state-root>/identity/device.key` with mode 0600 → create self-signed X.509 certificate → write certificate PEM to `<state-root>/identity/device.crt` → compute SPKI SHA-256 fingerprint → display peer ID. On subsequent init, load existing identity and display peer ID (unless `--reset-identity`).

2. **Pairing flow:** Listener opens TLS server socket with `spoink-pair/1` ALPN, presenting device certificate but not requiring verified peer → initiator connects with `spoink-pair/1` ALPN → both exchange PAIR_HELLO with `peer_id` and 32-byte `attempt_nonce` → derive SAS from TLS exporter `EXPORTER-Spoink-Pairing-v1` with context `min(peer_id)||max(peer_id)||min(nonce)||max(nonce)` → display 5-character hex SAS `AA-BBB` → both prompt user → exchange PAIR_CONFIRM → if both accepted, persist pin in DB-001 `trusted_peers` and exchange PAIR_RESULT `PAIRED` → close pairing connection. If either rejects or error, send PAIR_RESULT `REJECTED`, persist nothing, close. One attempt per listener.

3. **Normal connection flow:** TLS 1.3 mTLS with `spoink/1` ALPN → verify pinned SPKI fingerprint against DB-001 `trusted_peers` (non-revoked) → exchange HELLO with limits/capabilities → negotiate minor version and intersect limits.

4. **Transfer flow (M2 single file):**
   - Sender: select one file → chunk with rabin-v1 profile via M1 engine → build canonical manifest → connect to receiver → SESSION_OFFER with file summary → receive SESSION_ACCEPT with transfer_id → send MANIFEST_BEGIN/PART/END → receive TRANSFER_PLAN with missing chunk hashes → open one data connection → DATA_BIND with capability → send CHUNK_PUT for each missing chunk → receive CHUNK_ACK for each → receive FILE_RESULT → receive SESSION_RESULT → close.
   - Receiver: listen → accept mTLS connection → validate offer → accept → receive manifest → validate and persist graph (IC-009) → compute missing chunks (all for new transfer) → send TRANSFER_PLAN → receive DATA_BIND → for each CHUNK_PUT: decode (raw only in M2), verify hash/length, install via M1 store (IC-008), commit receipt (IC-009), send CHUNK_ACK → reconstruct file (IC-011) → verify whole-file SHA-256 → finalize (IC-011) → send FILE_RESULT → send SESSION_RESULT.

5. **Durable ACK semantics:** ACK is sent only after the chunk blob is atomically installed in the content store AND the session receipt is committed in SQLite. This is the existing M1 commit ordering (blob flush → rename → dir flush → DB transaction → ACK). A crash before ACK causes safe retry; a crash after ACK preserves the chunk.

6. **No partial final file:** The VerifiedStagingFile type from M1 already prevents finalization of unverified staging data. The staging file is never visible at the destination path until atomic rename after whole-file verification succeeds.

## Reviewable implementation slices and exact sequence

Each slice leaves CMake/CI and all prior tests passing, adds focused tests first, and is independently inspectable. The Builder may make coherent commits or report checkpoints; the complete M2 review remains mandatory.

### Slice 1 — Identity initialization and key management

Implement ARCH-01 identity creation:
- Ed25519 key pair generation via OpenSSL 3.6.0 `EVP_PKEY_keygen(EVP_PKEY_ED25519)`
- Self-signed X.509v3 certificate creation (CN=display name, validity=10y, keyUsage=digitalSignature)
- Private key PEM write with mode 0600 and permission verification on load
- Certificate PEM write
- SPKI SHA-256 fingerprint computation (peer ID derivation)
- `load_or_create_identity()` (IC-001): create if absent, load if present, reject if wrong type
- Weak key rejection: only Ed25519 accepted; any other EVP_PKEY type is an error
- Key/cert file integrity verification on load

**Files:** `include/spoink/identity/identity.h`, `src/identity/identity.cpp`, tests.
**Gate:** Generate key, create cert, compute peer ID matching hex display; reject non-Ed25519 keys; reject insecure permissions; load existing identity idempotently; no private key in stdout/logs (TEST-SEC-007 subset).

### Slice 2 — Pairing protocol and trust management

Implement API-001 pairing and peer trust:
- TLS server/client setup with `spoink-pair/1` ALPN, presenting device certificate, provisional acceptance
- PAIR_HELLO exchange with peer_id and attempt_nonce validation
- TLS exporter SAS derivation: `SSL_export_keying_material()` with label `EXPORTER-Spoink-Pairing-v1`
- SAS display: 20-bit → 5-hex-char `AA-BBB` format
- PAIR_CONFIRM and PAIR_RESULT exchange
- Pin persistence in DB-001 trusted_peers (IC-001 `pin_peer()`)
- Revocation (IC-001 `revoke_peer()`) — local, immediate for new connections
- Single-attempt listener enforcement
- Same-key certificate renewal acceptance (SPKI pin unchanged)
- New-key rejection requiring re-pairing

**Depends on:** Slice 1 (identity), M1 MetadataStore (DB-001 table exists in schema 1).
**Files:** `include/spoink/pairing/pairing.h`, `src/pairing/pairing.cpp`, tests.
**Gate:** TEST-SEC-001 (MITM proxy → different SAS), TEST-SEC-002 (unknown/revoked/changed identity rejection), TEST-SEC-004 (revocation confirmation, no secret output), TEST-E2E-001 (pair through loopback TLS).

### Slice 3 — Wire protocol framing and codec

Implement IC-006 protocol codec:
- 32-byte frame prefix serializer/deserializer with all fields (magic SPNK, major, minor, channel, message_type, flags, reserved, header_length, payload_length, request_id)
- Frame prefix validation: magic, version range, channel range, reserved=0, lengths against negotiated/absolute ceilings
- CBOR header decoder for each message type (schema-aware, deterministic)
- CBOR header encoder for each message type
- Partial TCP read state machine: accumulate exactly prefix+header+payload bytes before dispatching
- Limit-first allocation: validate header_length and payload_length against absolute ceilings before allocating buffers
- Unknown required message type → PROTOCOL_UNKNOWN_MESSAGE error
- All absolute ceiling constants as compiled values

**Files:** `include/spoink/protocol/framing.h`, `src/protocol/framing.cpp`, `include/spoink/protocol/codec.h`, `src/protocol/codec.cpp`, tests, fuzz corpus.
**Gate:** Fuzz corpus for prefix/CBOR parsing with sanitizers; golden frame fixtures across GCC/Clang and debug/release; TEST-SEC-006 (oversized lengths, malformed CBOR, reserved nonzero); correct partial-read reassembly.

### Slice 4 — Protocol state machine

Implement IC-007 session controller:
- Deterministic state machine driven by typed messages and effect callbacks
- HELLO exchange and capability/limits negotiation (API-002)
- SESSION_OFFER/ACCEPT/REJECT (API-003, API-004)
- MANIFEST_BEGIN/PART/END paged transfer (API-005)
- TRANSFER_PLAN (API-006)
- CHUNK_PUT/ACK/NACK (API-007, API-008)
- FILE_RESULT (API-011)
- SESSION_RESULT (API-011)
- CANCEL (API-010)
- ERROR (any channel)
- PING/PONG keepalive (API-010)
- Message ordering enforcement: HELLO first, then session messages in order, one active session
- Effect-based I/O: state machine produces effects (send, persist, prompt, timer); tests drive without sockets/filesystems
- One-session binding per connection

**Files:** `include/spoink/session/state_machine.h`, `src/session/state_machine.cpp`, tests.
**Gate:** All state transitions tested; illegal message ordering produces PROTOCOL_STATE error; deterministic behavior for same input sequence; session state matches domain model states (Offered→Accepted→Transferring→Verifying→Completed and error paths).

### Slice 5 — Transport layer and connection binding

Implement ARCH-06 TLS transport:
- TLS 1.3 connection setup with `spoink/1` ALPN for normal mode
- mTLS enforcement: both peers present certificates, verify pinned SPKI fingerprint
- Listener with configurable address/port, handshake timeout, concurrent handshake limit
- DATA_BIND / DATA_BIND_RESULT exchange for data connections (API-007)
- Session capability generation (32-byte CSPRNG), constant-time comparison, memory-only lifetime
- Connection idle timeout and PING/PONG scheduling
- Connection pool management (1 data connection for M2)
- TLS context configuration: TLS 1.3 only, ed25519 signature algorithm only, no 0-RTT, no TLS compression

**Depends on:** Slices 1–4.
**Files:** `include/spoink/transport/connection.h`, `src/transport/connection.cpp`, tests.
**Gate:** TEST-SEC-003 (wrong/expired/crossed capability rejection, excessive data connections), TEST-INT-004 (control + data TLS binding to one session), handshake timeout enforcement, concurrent handshake limit.

### Slice 6 — Send and receive orchestration with CLI

Implement end-to-end transfer orchestration:
- **Sender CLI (`spoink send`):** File selection (M1 IC-002, single file for M2), chunk with rabin-v1 (M1 IC-003), build manifest (M1 IC-004), connect, offer, send manifest parts, stream CHUNK_PUT, track ACKs, display progress, report result.
- **Receiver CLI (`spoink receive`):** Listen, authenticate, display offer and prompt approval (default reject), accept, receive and validate manifest, persist graph (M1 IC-009 `accept_manifest`), compute missing set, send TRANSFER_PLAN, receive and validate CHUNK_PUT, durable commit (M1 IC-008 `commit_verified_chunk`), send CHUNK_ACK, reconstruct (M1 IC-011), verify whole-file SHA-256, finalize (M1 IC-011), send FILE_RESULT/SESSION_RESULT.
- **CLI (`spoink init`, `spoink pair`):** Thin wrappers around identity and pairing modules.
- **CLI (`spoink peers list/revoke`):** Display peer info, revoke with confirmation.

**Depends on:** Slices 1–5 and M1 offline engine.
**Files:** `src/cli/` commands, `include/spoink/send/sender.h`, `src/send/sender.cpp`, `include/spoink/receive/receiver.h`, `src/receive/receiver.cpp`, tests.
**Gate:** TEST-INT-001 (pair + transfer empty/small file), TEST-E2E-002 single-file subset (byte-identical output), TEST-E2E-004 (existing destination reject and overwrite).

### Slice 7 — Durable ACK, fault tolerance, and whole-file verification

Harden the transfer path with fault injection:
- Verify ACK-after-durable-commit invariant at every boundary
- Fault injection: terminate/fail before temp write, during write, before flush, before rename, before DB commit, before ACK send, during reconstruction, during finalization
- Verify: no partial final file after any crash point
- Verify: acknowledged chunks survive restart via M1 reconciliation
- Verify: corrupt/malformed frames → rejection without crash
- Verify: idempotent chunk resubmission after lost ACK (FR-019)
- Empty file transfer (zero chunks, SHA-256 of empty input)

**Depends on:** Slice 6.
**Files:** Fault injection tests, extended integration tests.
**Gate:** TEST-FAULT-001 (connection drop at multiple points; resume not in M2 but state must be correct), TEST-FAULT-002 (chunk store durable boundaries), TEST-FAULT-003 (reconstruction/finalization), TEST-FAULT-004 (disk-full/permission), TEST-FAULT-005 (lost ACK → idempotent retry), TEST-FAULT-006 (corrupt blob → quarantine).

### Slice 8 — Integration, golden fixtures, and M2 closure

Bring everything together for acceptance:
- Two-process loopback E2E tests: empty, small (< min_bytes), and large (multiple chunks) single files
- Unknown/changed peer rejection E2E
- Corrupt/malformed frame rejection E2E
- Protocol-major-1 golden frame fixtures: freeze representative frames for HELLO, SESSION_OFFER, CHUNK_PUT, CHUNK_ACK, FILE_RESULT, SESSION_RESULT; verify cross-build (GCC/Clang, debug/release)
- Documentation updates: CLI examples for init/pair/send/receive, key policy (Ed25519), absolute parser ceilings, operational pairing runbook
- Limits verification: confirm rabin-v1 max_bytes fits negotiated payload ceiling
- Update traceability, risks, readiness, and consistency review docs
- M2 Builder report in agent/builder/implementation-report.md

**Depends on:** Slices 1–7.
**Files:** Tests, documentation, golden fixtures.
**Gate:** All M2 acceptance evidence items 1–10 from agent/tasks/M2-001.md. Independent Reviewer PASS required for item 11.

## Files expected to change

- **Build/dependencies:** CMakeLists.txt (new targets for identity, pairing, protocol, transport, session, send, receive, CLI commands), vcpkg.json (no new dependencies needed — OpenSSL 3.6.0 already pinned), third_party/NOTICE.md if any notice update needed.
- **Source (new):** include/spoink/identity/, src/identity/, include/spoink/pairing/, src/pairing/, include/spoink/protocol/, src/protocol/, include/spoink/transport/, src/transport/, include/spoink/session/, src/session/, include/spoink/send/, src/send/, include/spoink/receive/, src/receive/, src/cli/ (init, pair, send, receive, peers commands).
- **Source (extend):** include/spoink/support/ may gain small TLS/network utility types. include/spoink/offline/offline.h is not modified — M2 consumes M1 interfaces as-is.
- **Schema:** No migration needed — schema/001_initial.sql already contains trusted_peers (DB-001) and all required tables. M2 uses the existing schema.
- **Tests:** tests/unit/ (identity, pairing, framing, codec, state machine), tests/integration/ (pair+transfer, connection binding), tests/e2e/ (two-process loopback), tests/fault/ (durable boundaries), tests/fuzz/ (frame parser, CBOR decoder), tests/security/ (MITM, identity, capability, paths, log scan).
- **Fixtures:** tests/fixtures/ for golden frame bytes, test certificates/keys (clearly marked as test-only).
- **Documentation:** docs/16_operations/ (CLI examples, pairing runbook), docs/24_consistency_review.md, docs/25_implementation_readiness_review.md, docs/00_governance/00_03_traceability_matrix.md if it exists.
- **Handoff:** agent/builder/implementation-report.md (M2-001, written by Builder), agent/active-task.md (updated by Owner).

## Compatibility, migration, security, and failure considerations

### Compatibility
- Protocol major 1 and the frame/parser ceilings defined above become the compatibility contract. A future minor version may add messages or optional fields but must not reinterpret existing required fields or reduce these ceilings.
- Golden frame fixtures ensure deterministic frame bytes across GCC/Clang and debug/release builds.
- Schema 1 is unchanged from M1; no migration is required for M2. Future schema changes get version 2+ with tested migration.

### Security
- Ed25519 device keys: CSPRNG generation, restrictive file permissions, never logged/output.
- TLS 1.3 only, no fallback. ed25519 signature algorithm only. No 0-RTT.
- Session capabilities: 32 random bytes, memory-only, constant-time comparison, rotated on new epoch.
- Pairing SAS: derived from TLS exporter, not transmitted as comparable plaintext.
- All frame/CBOR parsing is limit-first: validate lengths before allocation.
- Path validation uses M1's existing normalized-path and containment logic.
- Private keys, capabilities, and payload content must not appear in logs at any level (TEST-SEC-007).

### Failure
- M2 does not implement resume (M3). A failed M2 transfer leaves the session in a terminal state (Failed or Cancelled). The receiver's durable state is preserved for diagnostic inspection.
- Receiver stops ACKing on store/disk failure and reports clearly.
- No partial final file under any failure mode — guaranteed by VerifiedStagingFile type and atomic finalization.
- Startup reconciliation (M1) handles crash recovery for blob store and DB.

### What M2 does NOT do
- No multi-file batch (M3)
- No delta/dedup selection (M3) — receiver always requests all chunks for the new transfer
- No resume after disconnect (M3) — session fails
- No compression (M4) — raw codec only
- No parallel data connections (M4) — one data connection
- No adaptive scheduling (M4) — static single-connection pipeline
- No telemetry dashboard, benchmark claims, or packaging (M5)
- No LAN discovery, NAT traversal, relay, or GUI

## Acceptance and verification required

The Reviewer must independently verify M2 task acceptance against the actual branch diff, tests, docs, and CI. At minimum, these evidence items from agent/tasks/M2-001.md:

1. Two isolated processes initialize identity, pair via SAS confirmation, and reconnect using pinned mTLS.
2. Transfer of empty, small, and large single files succeeds with byte-identical verified output.
3. Unknown and changed peers fail safely with clear rejection.
4. Corrupt and malformed frames fail safely without crashing or accepting bad data.
5. No partial final file exists at the destination after any outcome.
6. All required tests pass: TEST-SEC-001–007, parser fuzz, TEST-INT-001/004, TEST-E2E-001/002/004 single-file subset.
7. Protocol-major-1 golden frames are frozen and cross-build verified.
8. Documentation updated: CLI examples, key policy, absolute parser ceilings, operational pairing runbook.
9. TBD-SEC-KEY-001 is resolved with evidence (Ed25519, documented in this plan).
10. M2-required TBD-LIMITS-001 ceilings are resolved with evidence (documented in this plan).
11. Independent Reviewer returns PASS with CI evidence.

Additionally verify:
- M0 and M1 tests remain green (no regression).
- ASan/UBSan clean on all new code.
- clang-tidy clean on all new code.
- No new dependency beyond pinned OpenSSL 3.6.0 is required for M2 TLS/identity.
- Golden frame bytes are deterministic across GCC/Clang and debug/release.
- Ed25519 key generation and TLS 1.3 mTLS work correctly with OpenSSL 3.6.0.

## Explicit assumptions

- A1, verified authority: agent/tasks/M2-001.md and the approved docs are authoritative. The external roadmap is only input evidence.
- A2, OpenSSL Ed25519: OpenSSL 3.6.0 as pinned in vcpkg.json supports Ed25519 key generation, self-signed certificate creation, and TLS 1.3 certificate authentication. If a build issue is discovered with the pinned version, stop and escalate.
- A3, single connection M2: M2 uses exactly one control connection and one data connection. Connection pooling (>1 data connection) belongs to M3/M4.
- A4, no resume: M2 transfers fail on disconnect. Resume requires M3 implementation. Session state is preserved for diagnostic purposes but not for automated restart.
- A5, raw codec only: M2 sends chunks as raw uncompressed bytes. The codec field in CHUNK_PUT is always "raw". Compression negotiation belongs to M4.
- A6, schema stability: Schema 1 from M1 is sufficient for M2. The trusted_peers, transfer_sessions, and related tables support M2 pairing and transfer state. No schema migration is needed.
- A7, test isolation: M2 tests use loopback TLS connections on ephemeral ports. No external network access is required. Test certificates and keys are clearly marked as fixtures.
- A8, filesystem: M2 targets the documented Linux local-filesystem semantics, same as M1. Platform-specific TLS/socket behavior is isolated behind adapters.

## Instructions and boundaries for the Builder

Implement the 8 slices in order, with focused verification and reviewable checkpoints. Select routine library APIs, private class layout, test harness form, and implementation choices within these boundaries:

1. **Ed25519 only.** Use OpenSSL 3.6.0 EVP_PKEY_ED25519 for key generation and TLS certificate authentication. Do not implement or accept any other key type.

2. **Absolute ceilings are compiled constants.** The frame/parser/session ceilings in the TBD-LIMITS-001 table above are compiled constants (constexpr or equivalent). Operational defaults may be configurable but must not exceed these ceilings. Negotiation clamps to ceilings.

3. **Protocol state machine is pure.** IC-007 must be testable without sockets or filesystems. Use effects/commands for I/O. Deterministic for the same ordered inputs.

4. **Reuse M1 interfaces.** Selection (IC-002), chunking (IC-003), manifest (IC-004), store (IC-008), metadata (IC-009), and reconstruction (IC-011) are consumed through their existing M1 interfaces. Do not fork or duplicate M1 code.

5. **TLS 1.3 only. No fallback.** Configure OpenSSL to accept only TLS 1.3. Disable 0-RTT. Restrict signature algorithms to ed25519. Disable TLS compression.

6. **One session, one data connection.** M2 enforces max_files=1, max_data_connections=1. Do not implement multi-file or multi-connection scheduling. Leave those paths for M3/M4.

7. **No resume.** M2 transfers that lose connection fail. Preserve session state for diagnostics but do not implement RESUME_REQUEST/ACCEPT/REJECT handling.

8. **Fuzz everything that parses untrusted input.** Frame prefix, CBOR headers, and manifest CBOR all require fuzz targets with sanitizers. Crashes become permanent regression corpus entries.

9. **Golden frames before M2 closure.** Freeze representative protocol-major-1 frames and verify them across GCC/Clang and debug/release before claiming M2 complete.

10. **Preserve M0/M1 audit history.** Do not alter agent/task.md, agent/tasks/M1-001.md, agent/builder/implementation-report.md (M1 content), or agent/reviewer/review.md (M1 content). Write the M2-001 Builder report in agent/builder/implementation-report.md by replacing the M1 content or adding an M2 section as appropriate.

11. **Escalate conflicts.** If you discover a conflict between this plan and an accepted ADR, product requirement, or wire contract that cannot be resolved within the specified boundaries, stop and present the evidence to the Owner with exact options and trade-offs, marked HUMAN_DECISION_REQUIRED.
