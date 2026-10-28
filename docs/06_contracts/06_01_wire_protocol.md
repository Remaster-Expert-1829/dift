# Wire Protocol

> Status: Approved for protocol major `1`; release safety ceilings remain `TBD-LIMITS-001`.  
> Transport: TCP with TLS 1.3.  
> Encoding decision: ADR-007.

## Connection profiles

| ALPN | Purpose |
|---|---|
| `spoink-pair/1` | Explicit first-time pairing only. No session or chunk messages allowed. |
| `spoink/1` | Normal paired-peer control or data connection. |

Both peers present device certificates. In normal mode, TLS verification requires a pinned non-revoked public-key identity. In pairing mode, provisional self-signed identities are admitted only to the restricted pairing state machine and become trusted only after SAS confirmation.

## Frame prefix

Every post-handshake application message begins with exactly 32 bytes:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | `magic` | ASCII `SPNK` (`53 50 4e 4b`). |
| 4 | 1 | `major` | Protocol major, `1`. |
| 5 | 1 | `minor` | Negotiated protocol minor, initially `0`. |
| 6 | 1 | `channel` | `0=PAIRING`, `1=CONTROL`, `2=DATA`. |
| 7 | 1 | `message_type` | Type from the registry below. |
| 8 | 2 | `flags` | Defined bits only; initially bit 0 `OPTIONAL`. |
| 10 | 2 | `reserved` | Must be zero; nonzero is malformed in major 1. |
| 12 | 4 | `header_length` | Unsigned big-endian bytes of following CBOR header. |
| 16 | 8 | `payload_length` | Unsigned big-endian bytes of raw following payload. |
| 24 | 8 | `request_id` | Unsigned big-endian correlation ID; zero only where stated. |

The receiver validates the fixed prefix and both lengths against local/negotiated limits **before allocation**. The header is one deterministic-CBOR map. Payload meaning depends on message type. Exact read handles partial TCP reads; trailing bytes belong to the next frame.

TLS provides transport integrity, so frames have no separate checksum. Content frames still require plaintext SHA-256 validation.

## Message registry

| Value | Name | Channel | Direction |
|---:|---|---|---|
| `0x01` | `HELLO` | Control/Data | Both |
| `0x02` | `ERROR` | Any | Both |
| `0x03` | `PING` | Control | Both |
| `0x04` | `PONG` | Control | Response |
| `0x10` | `PAIR_HELLO` | Pairing | Both |
| `0x11` | `PAIR_CONFIRM` | Pairing | Both |
| `0x12` | `PAIR_RESULT` | Pairing | Both |
| `0x20` | `SESSION_OFFER` | Control | Sender → receiver |
| `0x21` | `SESSION_ACCEPT` | Control | Receiver → sender |
| `0x22` | `SESSION_REJECT` | Control | Receiver → sender |
| `0x23` | `MANIFEST_BEGIN` | Control | Sender → receiver |
| `0x24` | `MANIFEST_PART` | Control | Sender → receiver |
| `0x25` | `MANIFEST_END` | Control | Sender → receiver |
| `0x26` | `TRANSFER_PLAN` | Control | Receiver → sender |
| `0x27` | `RESUME_REQUEST` | Control | Sender → receiver |
| `0x28` | `RESUME_ACCEPT` | Control | Receiver → sender |
| `0x29` | `RESUME_REJECT` | Control | Receiver → sender |
| `0x2a` | `CANCEL` | Control | Both |
| `0x2b` | `BACKPRESSURE` | Control | Receiver → sender |
| `0x2c` | `FILE_RESULT` | Control | Receiver → sender |
| `0x2d` | `SESSION_RESULT` | Control | Receiver → sender |
| `0x40` | `DATA_BIND` | Data | Sender → receiver |
| `0x41` | `DATA_BIND_RESULT` | Data | Receiver → sender |
| `0x42` | `CHUNK_PUT` | Data | Sender → receiver |
| `0x43` | `CHUNK_ACK` | Data | Receiver → sender |
| `0x44` | `CHUNK_NACK` | Data | Receiver → sender |

Unregistered types without `OPTIONAL` produce `PROTOCOL_UNKNOWN_MESSAGE` and close that connection. An optional unknown message may be skipped only after its bounded frame is consumed and only when negotiated minor-version rules allow it.

## Common header fields

CBOR map keys are lowercase ASCII text. Required common fields where applicable:

- `transfer_id`: 16-byte byte string.
- `peer_id`: 32-byte byte string; MUST equal the TLS certificate SPKI fingerprint.
- `protocol_min` / `protocol_max`: `[major, minor]` integer arrays.
- `manifest_digest`: 32-byte byte string.
- `chunk_hash`: 32-byte byte string.
- `error_code`: stable uppercase ASCII code.
- `retryable`: Boolean.
- `retry_after_ms`: non-negative integer, present only when meaningful.

Unknown fields are rejected in minor 0 unless their key begins `x-` and the containing message permits extension fields. Security/correctness decisions never depend on ignored extension fields.

## API-001 — Pairing

1. Both peers exchange `PAIR_HELLO` with `peer_id`, bounded `display_name`, and 32-byte random `attempt_nonce`. Values must match the presented certificate and local attempt.
2. Both derive 32 bytes using the TLS exporter label `EXPORTER-Spoink-Pairing-v1` and context `min(peer_id)||max(peer_id)||min(nonce)||max(nonce)` in bytewise order.
3. The SAS is the first 20 exporter bits displayed as exactly five uppercase hexadecimal characters, grouped `AA-BBB`. Both screens must match.
4. After local user input, each sends `PAIR_CONFIRM {accepted: bool}`.
5. If both accepted, each persists the pin and sends `PAIR_RESULT {status: "PAIRED"}`. Otherwise both return `REJECTED` and persist nothing.

Only these messages and `ERROR` are valid under pairing ALPN. Pairing accepts one TLS connection per explicit attempt; failure closes the listener attempt. A crash may leave asymmetric pins; running pairing again after inspecting/revoking the partial record repairs it.

## API-002 — HELLO and capability negotiation

The first normal application message on each connection is `HELLO` with:

```text
{
  peer_id: bstr32,
  role: "CONTROL_SENDER" | "CONTROL_RECEIVER" | "DATA_SENDER" | "DATA_RECEIVER",
  protocol_min: [1,0],
  protocol_max: [1,0],
  manifest_schemas: [1],
  codecs: ["raw", "lz4", "zstd"],
  limits: {
    max_header_bytes, max_payload_bytes, max_manifest_bytes,
    max_files, max_chunks, max_path_bytes
  },
  instance_nonce: bstr32
}
```

Control receiver selects the highest mutually supported minor under major 1 and intersects capabilities/limits by minimum. No overlap returns `PROTOCOL_NO_COMMON_VERSION` or `PROTOCOL_NO_COMMON_CAPABILITY`.

## API-003 — Session offer

`SESSION_OFFER` uses nonzero `request_id`:

```text
{
  manifest_digest: bstr32,
  manifest_schema: 1,
  chunk_profile_id: text,
  file_count: uint,
  logical_bytes: uint,
  overwrite_requested: bool,
  files: [{file_id, relative_path, logical_size, file_sha256}]
}
```

The file summaries must fit negotiated limits and match the later manifest. No file bytes are accepted yet.

## API-004 — Session acceptance/rejection

`SESSION_ACCEPT` echoes `request_id` and contains receiver-generated `transfer_id`, accepted `file_ids`, effective overwrite policy, and manifest transfer limits. Acceptance is provisional until the full manifest is validated and persisted.

`SESSION_REJECT` echoes `request_id` and contains stable reason plus optional per-file rejections. Rejected offers are not resumable.

## API-005 — Manifest transfer

- `MANIFEST_BEGIN`: `transfer_id`, `manifest_digest`, `total_bytes`, `part_count`.
- `MANIFEST_PART`: `transfer_id`, zero-based `part_index`, `part_offset`; payload is that exact slice of canonical manifest bytes.
- `MANIFEST_END`: `transfer_id`, `manifest_digest`.

Parts must arrive once and cover the exact byte range without gaps/overlap. The receiver hashes the reassembled bounded bytes, decodes deterministic CBOR, validates every invariant and summary, and persists the accepted graph transactionally. Failure sends `ERROR` and marks the provisional session failed/rejected.

## API-006 — Transfer plan

After persistence, receiver sends one or more `TRANSFER_PLAN` messages:

```text
{
  transfer_id: bstr16,
  session_capability: bstr32,       // first part only; secret
  plan_part: uint,
  plan_final: bool,
  missing_chunk_hashes: [bstr32...],
  effective_limits: {
    max_data_connections, max_in_flight_chunks, max_in_flight_bytes
  }
}
```

The capability is generated by the OS CSPRNG, exists only in receiver/sender memory for the control epoch, is never logged, and is replaced on resume. Missing hashes must belong to accepted files. A missing-set digest MAY be included as an `x-` extension but is not authoritative in minor 0.

## API-007 — Data bind and chunk submission

On a data connection, normal `HELLO` is followed by:

```text
DATA_BIND {
  transfer_id: bstr16,
  session_capability: bstr32,
  connection_nonce: bstr32
}
```

Receiver verifies TLS peer ID equals the control peer, capability is current, session is transferable, and pool limit is not exceeded. It returns `DATA_BIND_RESULT {accepted, error_code?}`. The capability uses constant-time comparison.

`CHUNK_PUT` header:

```text
{
  transfer_id: bstr16,
  chunk_hash: bstr32,
  plaintext_length: uint,
  codec: "raw" | "lz4" | "zstd",
  encoded_length: uint,
  attempt: uint
}
```

Payload contains exactly `encoded_length` bytes and prefix `payload_length` must match. One v1 chunk occupies one frame; the negotiated payload limit therefore constrains the CDC profile maximum. The receiver rejects hashes not required by the accepted manifest or already invalidated session.

## API-008 — Chunk result

`CHUNK_ACK` echoes request ID and includes `transfer_id`, `chunk_hash`, `status` (`STORED` or `ALREADY_PRESENT`), and durable receiver timestamp. ACK has the durable meaning from FR-015.

`CHUNK_NACK` contains `error_code`, `retryable`, and optional bounded `retry_after_ms`. It never returns internal paths or sensitive diagnostics.

Duplicate `CHUNK_PUT` for an already verified session/hash returns `ALREADY_PRESENT` after verifying the catalog/blob state. A duplicated request ID with a different hash/frame is `PROTOCOL_ID_REUSE`.

## API-009 — Resume

After normal control `HELLO`, sender sends:

```text
RESUME_REQUEST {
  transfer_id: bstr16,
  manifest_digest: bstr32,
  sender_peer_id: bstr32
}
```

`RESUME_ACCEPT` returns a new capability, current accepted file states, paged missing hashes, and effective limits. `RESUME_REJECT` returns one of `SESSION_NOT_FOUND`, `PEER_MISMATCH`, `MANIFEST_MISMATCH`, `SESSION_EXPIRED`, `SESSION_TERMINAL`, or `PROTOCOL_INCOMPATIBLE`. Rejection never silently creates a new session.

## API-010 — Flow control, ping, and cancellation

- `PING {monotonic_token}` receives immediate `PONG` echo; tokens are telemetry only.
- `BACKPRESSURE {reason, retry_after_ms?, max_in_flight_bytes?}` may reduce sender activity but cannot increase negotiated limits.
- `CANCEL {transfer_id, scope: "SESSION"|"FILE", file_id?, reason}` is idempotent. Peer replies through result/error messages. Cancellation does not imply deletion of already completed files.

## API-011 — Results

`FILE_RESULT` contains file ID/path, terminal state, logical size, verified digest when completed, bytes transferred/reused, and stable error code when not completed.

`SESSION_RESULT` contains overall state, per-state file counts, logical/useful/reused/encoded bytes, retry count, duration, and whether receiver retained resumable state. “Completed” is sent only after receiver metadata records every accepted file complete.

## Error behavior

| Class | Examples | Connection/session action |
|---|---|---|
| Authentication | `AUTH_UNKNOWN_PEER`, `AUTH_REVOKED`, `AUTH_IDENTITY_CHANGED` | Close before normal application work; never retry automatically. |
| Protocol | `PROTOCOL_MALFORMED`, `PROTOCOL_LIMIT_EXCEEDED`, `PROTOCOL_STATE`, `PROTOCOL_ID_REUSE` | Send bounded error when safe, then close affected connection; repeated violations fail session. |
| Session | `SESSION_NOT_FOUND`, `MANIFEST_MISMATCH`, `DEST_EXISTS` | Reject operation; control connection may remain for terminal result. |
| Data | `CHUNK_HASH_MISMATCH`, `DECOMPRESSION_FAILED`, `CHUNK_NOT_REQUESTED` | NACK; retry only per classification/budget. |
| Resource | `RECEIVER_BUSY`, `STORAGE_CAPACITY`, `LIMIT_EXCEEDED` | Backpressure, pause, or reject as documented; no unbounded retry. |
| Internal/store | `STORE_IO`, `STORE_CORRUPT`, `INTERNAL` | Stop ACKs, pause/fail safely, log local detail under correlation ID. |

## Versioning

- Major mismatch: fail before session negotiation.
- Minor negotiation: highest mutually supported within major.
- Minor changes are additive; they cannot reinterpret an existing required field or error code.
- Required unknown field/type: fail explicitly.
- Manifest schema is negotiated independently but is bound into manifest bytes/digest.
- Resume requires compatible major and manifest schema. Representation/codec may change on retry because it does not change logical content.

## Limits condition

`TBD-LIMITS-001` must select and test default and absolute ceilings for frame headers/payloads, manifests, file/chunk counts, paths, concurrent handshakes, and timeouts before untrusted-network testing. Until then, builds MUST require explicit developer limits and MUST NOT claim protocol freeze or release readiness.
