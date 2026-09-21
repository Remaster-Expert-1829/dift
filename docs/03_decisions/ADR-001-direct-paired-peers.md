# ADR-001 — Direct Paired-Peer Transfer

- **Status:** Accepted
- **Date:** 2026-09-22

## Context

The source roadmap used client/server wording, but the approved product direction is ShareIt-style direct transfer. Users do not require cloud storage or access while the other device is offline. A central server would add hosting, accounts, authorization, operations, and privacy concerns unrelated to the desired systems project.

## Decision

DeltaSync v1 has no permanent central server. Each installed device has the same peer application and persistent identity. For each session, one device temporarily listens as receiver and the other connects as sender. The roles can reverse in a later session.

V1 uses explicit LAN addressing. One session has exactly one sender and one receiver. Automatic discovery, NAT traversal, relays, multi-source transfer, and automatic bidirectional synchronization are outside v1.

## Alternatives considered

- **Permanent central server:** simplifies availability and discovery but adds infrastructure and a different trust/product model.
- **Peer-to-peer swarm:** increases source availability but requires discovery, piece availability, coordination, and broader threat handling.
- **Automatic two-way synchronization:** requires version/conflict/tombstone semantics not present in the product definition.

## Consequences

- No paid or always-on service is required.
- Both devices must be online and mutually reachable.
- Every receiver maintains its own metadata/chunk store.
- “Upload” and “download” are replaced by session-local send/receive roles.
- Local-network firewall instructions and manual address handling are operational requirements.

## Migration implications

A future relay or cloud node must be introduced as a new peer/deployment role with an ADR. It cannot silently become authoritative storage. Multi-source or automatic sync requires new domain states and protocol contracts.
