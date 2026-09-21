# ADR-005 — Control and Data Connections

- **Status:** Accepted
- **Date:** 2026-09-22

## Context

DeltaSync needs negotiation, durable progress, parallel chunk delivery, and adaptive concurrency. Sharing one byte stream for large payloads and control messages creates head-of-line and framing pressure. Opening an unbounded connection per chunk is unsafe and difficult to recover.

## Decision

Each transfer session uses one long-lived mTLS control connection and a bounded pool of mTLS data connections. The receiver issues a cryptographically random session capability over the control connection. Each data connection proves the same pinned peer identity and presents the transfer ID plus capability before payload frames.

The scheduler may grow or shrink the data pool within configured bounds. Multiple files share the pool. Each chunk submission is independently identified and idempotent. Control-plane loss pauses new work; established data writes finish or are abandoned according to the failure policy, and resume establishes a new control connection/capability.

## Alternatives considered

- **Single connection:** simpler but couples control and bulk data and cannot express the intended connection adaptation.
- **One connection per file:** file size distribution causes imbalance and ties concurrency to file count.
- **One connection per chunk:** excessive handshakes/resources and poor congestion behavior.
- **UDP/custom reliability or QUIC:** materially broader protocol and dependency scope than v1.

## Consequences

- Session-binding and connection limits are security-critical.
- TLS handshake cost and multiple TCP congestion loops must be measured; a static one-data-connection mode remains available.
- A control/data frame contract and deterministic retry ownership are required.
- Parallelism is bounded across sessions to avoid exhausting the receiver.

## Migration implications

QUIC or a single multiplexed transport would require a new protocol major/capability and ADR. The logical manifest and store can remain unchanged.
