# Architecture Decision Records

> Status: Approved index

| ADR | Status | Decision |
|---|---|---|
| [ADR-001](ADR-001-direct-paired-peers.md) | Accepted | Direct paired-peer transfer with temporary sender/receiver roles |
| [ADR-002](ADR-002-stable-logical-chunks.md) | Accepted | Immutable CDC logical chunks separated from adaptive transport policy |
| [ADR-003](ADR-003-tls-device-identity.md) | Accepted | TLS 1.3 device identities, verified first pairing, and pinning |
| [ADR-004](ADR-004-hybrid-local-store.md) | Accepted | SQLite metadata plus filesystem content-addressed chunk store |
| [ADR-005](ADR-005-control-and-data-connections.md) | Accepted | One control connection plus bounded data-connection pool |
| [ADR-006](ADR-006-configurable-compression-policy.md) | Accepted | Compression algorithms fixed as capabilities; routing parameters remain tunable |
| [ADR-007](ADR-007-versioned-framing-and-cbor.md) | Accepted | Versioned fixed framing with deterministic CBOR metadata/manifests |

ADRs describe decisions, not every implementation choice. To change an accepted decision, add a superseding ADR and update affected requirements, contracts, risks, and tests.
