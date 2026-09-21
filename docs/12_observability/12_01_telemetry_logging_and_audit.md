# Telemetry, Logging, and Audit

> Status: Approved

## Objectives

- Explain transfer progress, failures, scheduler/compression choices, and recovery.
- Support reproducible benchmarks without packet inspection where application measurements suffice.
- Correlate both peers by transfer ID while avoiding secrets and unnecessary path/content disclosure.
- Ensure observability failure cannot corrupt transfer behavior.

## Structured event envelope

Every persisted/JSONL event uses:

```text
{
  schema: 1,
  timestamp_utc: RFC3339 string,
  monotonic_ns?: uint64,
  level: "debug" | "info" | "warn" | "error",
  event: stable identifier,
  process_instance_id: hex,
  local_peer_id_short: hex,
  remote_peer_id_short?: hex,
  transfer_id?: canonical text,
  connection_id?: local opaque text,
  file_id?: uint,
  chunk_id_short?: hex,
  fields: object
}
```

Short IDs are for correlation only and use enough prefix characters under a locally collision-checked mapping; collisions cause longer display, never ambiguous internal identity.

## Event catalog

| Family | Required examples |
|---|---|
| Identity/audit | `identity.created`, `pairing.started`, `pairing.confirmed`, `pairing.rejected`, `peer.revoked` |
| Connection | `connection.opened`, `connection.auth_failed`, `connection.closed`, `connection.timeout` |
| Session | `session.offered`, `session.accepted`, `session.rejected`, `session.paused`, `session.resumed`, `session.completed`, `session.failed`, `session.cancelled` |
| File | `file.selected`, `file.rejected`, `file.reconstructing`, `file.verified`, `file.finalized`, `file.failed` |
| Chunk/store | `chunk.requested`, `chunk.acked`, `chunk.nacked`, `chunk.reused`, `chunk.quarantined`, `store.reconciled`, `gc.summary` |
| Compression | `compression.skipped`, `compression.encoded`, `compression.discarded`, `compression.failed` |
| Scheduler | `scheduler.snapshot`, `scheduler.probe`, `scheduler.changed`, `scheduler.degraded`, `scheduler.static_fallback` |
| Recovery | `recovery.started`, `recovery.orphan_adopted`, `recovery.receipt_invalidated`, `recovery.completed` |
| Security | `security.protocol_violation`, `security.limit_rejected`, `security.path_rejected`, `security.integrity_failure` |

High-volume chunk events are sampled/aggregated in normal mode, except failures and final counters. Debug mode may emit them but still redacts sensitive values.

## Metrics

### Counters

- sessions/files/chunks offered, accepted, completed, failed, cancelled, resumed;
- logical, useful, reused, encoded, application-frame, and observed wire bytes;
- chunk retries/NACKs by stable reason;
- connection attempts/failures/timeouts;
- compression attempts/skips/accepted/discarded and CPU time by codec;
- store additions/reuses/quarantines/GC deletions;
- protocol/security limit rejections;
- dropped telemetry events.

### Gauges

- active sessions/control/data connections;
- ready/in-flight chunks and bytes;
- worker queue bytes/items;
- store logical/physical bytes and filesystem free bytes;
- current scheduler state/targets;
- SQLite busy operations/current leases.

### Histograms/distributions

- application probe RTT;
- durable chunk ACK latency;
- chunk encode/decode/hash/store time;
- encoded-to-plaintext ratio;
- file reconstruction/finalization duration;
- SQLite transaction/busy duration;
- session throughput over defined windows.

Metrics have documented units (`bytes`, `seconds`, counts) and bounded labels. File path, chunk hash, transfer ID, and peer ID are not metric labels.

## Transfer report

Each terminal/paused session can produce schema-versioned JSON containing:

- software/protocol/manifest/CDC/compression/scheduler profile versions;
- local/remote short IDs and transfer ID;
- start/end/duration and terminal state;
- file counts/states and logical bytes;
- chunk counts: total, requested, stored, reused, retried, failed;
- byte categories with measurement method;
- compression outcomes and CPU time;
- connection/scheduler timeline summary;
- errors by stable code;
- environment snapshot safe for benchmarks (CPU model, OS/kernel, library versions) only when explicitly enabled.

Unavailable data is `null`, not zero. Calculated ratios include numerator/denominator and exclude categories explicitly stated.

## Tracing

V1 uses local structured spans/events rather than requiring a distributed tracing backend. `request_id`, connection ID, transfer ID, file ID, and shortened chunk ID correlate operations. Optional OpenTelemetry export may be added later without making a collector required.

## Audit behavior

Pairing, revocation, destructive maintenance, overwrite acceptance, authentication failures, integrity failures, and terminal session results are audit events. Local audit storage is append-oriented and retention-controlled. V1 audit logs are not tamper-proof and MUST NOT be advertised as forensic non-repudiation.

## Data restrictions

Never log:

- device private keys or TLS session keys;
- session capabilities;
- file contents or chunk payloads;
- full certificates unless an explicit local diagnostic export is requested;
- plaintext SAS after pairing completes;
- raw source/destination roots in default persistent logs;
- environment variables or complete configuration dumps containing secret paths beyond what diagnostics require.

Relative paths are shown interactively but omitted or optionally hashed in persistent logs. Full chunk/peer hashes are shown only by explicit inspection commands, not routine logs.

## Performance and failure

- Telemetry queues are bounded.
- Low-priority events may drop under load; drops are counted.
- Terminal summaries and security/integrity events use reserved capacity and synchronous fallback to stderr if persistence fails.
- Benchmark mode records enough detail for evidence but measures/declares telemetry overhead.
- Log I/O failure never turns an invalid chunk into valid state; if a required audit/terminal record cannot be produced, the transfer result notes observability degradation.
