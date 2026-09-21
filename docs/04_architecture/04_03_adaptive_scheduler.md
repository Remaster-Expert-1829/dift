# Adaptive Scheduler

> Status: Approved architecture; tuning values remain experimental.  
> Requirements: FR-012, FR-013, NFR-003, NFR-006, NFR-007

## Responsibility

The scheduler selects how aggressively already-defined missing chunks are transferred. It does not infer packet loss from TCP internals, alter CDC boundaries, accept corrupt content, or override resource/security limits.

## Inputs

At each configurable evaluation interval, the scheduler receives an immutable snapshot:

| Signal | Definition |
|---|---|
| `active_connections` | Authenticated data connections currently usable. |
| `in_flight_chunks/bytes` | Submitted but not durably acknowledged work. |
| `useful_bytes_acked` | Plaintext bytes durably acknowledged during the window. |
| `wire_payload_bytes` | Encoded payload bytes sent during the window. |
| `probe_rtt` | Control ping-to-pong application round-trip samples. |
| `ack_latency` | Chunk-submit completion-to-durable-ACK samples. |
| `timeout_count` | Application operations exceeding the configured deadline. |
| `connection_error_count` | Broken/failed data connections by retry class. |
| `queue_depth_bytes` | Ready work awaiting scheduling. |
| `receiver_backpressure` | Explicit slow-down/storage-pressure signal from receiver. |

The scheduler reports **timeout/error rate**, not network packet loss, because TCP does not expose reliable packet-loss information through application ACK gaps.

## Outputs

- target active data-connection count;
- maximum in-flight chunks and bytes;
- per-retry not-before time;
- fair next-chunk ordering across files;
- compression policy hint (`prefer_raw`, `prefer_fast`, or `prefer_compact`) that cannot override negotiated/operator codec mode;
- reason code and input snapshot for telemetry.

Every output is clamped to validated device/session bounds.

## State machine

| State | Purpose | Entry | Exit |
|---|---|---|---|
| `STATIC` | Operator-selected fixed behavior or safe fallback. | Adaptive disabled, missing signals, or internal scheduler fault. | Explicit successful re-enable/new session. |
| `STARTUP` | Collect baseline without aggressive probing. | New/resumed adaptive session. | Minimum valid observation windows collected. |
| `STEADY` | Hold current setting and observe. | Stable baseline/probe completion. | Probe eligibility or degradation. |
| `PROBE` | Add one connection or bounded in-flight increment. | Queue is non-empty, no receiver pressure, cooldown elapsed. | Evaluation accepts or rejects probe. |
| `DEGRADED` | Reduce pressure after timeouts/errors/receiver signal. | Configured degradation predicate. | Stable recovery windows. |
| `COOLDOWN` | Prevent oscillation after a change. | Accepted/rejected probe or reduction. | Cooldown duration elapsed. |

## Deterministic policy skeleton

```text
snapshot := aggregate completed evaluation window
if snapshot invalid or internal error:
    enter STATIC using configured safe values
else if receiver_backpressure or degradation predicate:
    target_connections := clamp(floor(current * decrease_factor))
    in_flight_bytes := clamp(floor(current_window * window_decrease_factor))
    enter DEGRADED then COOLDOWN
else if state permits probing and queue can use more capacity:
    remember baseline goodput and latency
    increase target_connections by exactly one (within bound)
    enter PROBE
else if probe window complete:
    accept only when useful-goodput gain meets minimum gain
      and latency/error guardrails remain satisfied
    otherwise restore previous target
    enter COOLDOWN
else:
    retain current outputs
```

`decrease_factor`, window factors, minimum gain, guardrails, aggregation method, EWMA coefficients, window count, and cooldown are `EXP-SCHED-*`. They MUST be named configuration/profile values, recorded in benchmarks, and never hidden literals.

## Fairness and ordering

- Ready chunks are grouped per file.
- The default queue uses deficit round-robin measured in plaintext bytes so large chunks/files do not monopolize all connections and small files make progress.
- Retries re-enter their file queue only after backoff; they do not permanently outrank new work.
- A file close to completion MAY receive a bounded configurable bonus only if starvation tests remain satisfied.
- Duplicate hash requirements across files map to one transfer task and multiple manifest references.

## Connection changes

- Growth opens at most one new connection per probe action.
- Shrink marks excess connections as draining; it does not cancel an in-flight durable write unless the connection has failed.
- TLS/authentication failure is not a congestion signal and is never retried adaptively.
- Resume starts in `STARTUP`; old connection state is telemetry history, not assumed current capacity.

## Retry policy interface

The scheduler receives a classified error:

- `TRANSIENT_NETWORK`: bounded exponential backoff with jitter.
- `RECEIVER_BUSY`: honor receiver retry-after within configured maximum.
- `INTEGRITY_MISMATCH`: one clean reread/re-encode attempt is allowed by policy; repeated mismatch fails.
- `AUTH`, `PROTOCOL`, `PATH`, `CAPACITY_HARD`, `SOURCE_CHANGED`: non-retryable without operator/action or new session.

Exact attempt counts and delays are configuration values constrained by the failure policy.

## Experimental selection

| ID | Parameter family | Resolution evidence |
|---|---|---|
| EXP-SCHED-001 | initial/min/max data connections | Stable-LAN sweep; CPU/memory/socket use; fairness. |
| EXP-SCHED-002 | evaluation/probe/cooldown windows | Step-change and oscillation simulation. |
| EXP-SCHED-003 | RTT/goodput aggregation and coefficients | Replay of benchmark traces with outliers. |
| EXP-SCHED-004 | degradation/guardrail thresholds | Delay, rate-limit, disconnect, and receiver-pressure tests. |
| EXP-SCHED-005 | in-flight byte/chunk bounds | Bandwidth-delay-product sweep and memory cap. |
| EXP-SCHED-006 | compression hints | CPU-versus-link bottleneck matrix. |

## Required verification

- Pure-policy tests with a fake clock and recorded snapshots.
- Invariants: outputs always bounded; no chunk identity mutation; no negative/overflow values.
- No-starvation test with mixed file/chunk sizes.
- Static mode parity test.
- Network-shaping experiments for delay/rate/disconnect. Linux `tc netem` packet loss MAY be used to stress TCP, but results are described as shaped conditions rather than scheduler-observed packet loss.
- Decision-event replay sufficient to explain each output transition.
