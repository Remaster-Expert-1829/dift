# Project Charter

> Status: Approved  
> Related: `FR-*`, `NFR-*`, `ADR-001`, `ADR-002`, `ADR-003`, `ADR-004`, `ADR-005`

## Project

**Name:** Spoink
**Classification:** Tier 3 complex systems project, deliberately scoped as a solo production-oriented prototype.

## Problem

Conventional direct file transfer resends complete files, cannot reliably resume after process or network interruption, and often applies fixed compression and concurrency policies without considering content or observed link behavior. Spoink should transfer batches of files directly between paired devices while reusing data already present on the receiver and preserving correctness under interruption.

## Target users

- A developer or technical user transferring files between devices they control.
- A reviewer evaluating systems programming, protocol design, resilience, security, and benchmark methodology.
- A small trusted team using paired devices on a controlled local network.

V1 is not designed for anonymous users, hostile storage providers, or an internet-facing multi-tenant service.

## Goals

| ID | Goal |
|---|---|
| G-001 | Send multiple regular files directly between two paired devices on the same reachable local network. |
| G-002 | Reuse receiver-held chunks so unchanged content is not retransmitted. |
| G-003 | Resume an interrupted transfer from durably verified receiver state. |
| G-004 | Preserve byte-for-byte file correctness and never expose an incomplete file as completed. |
| G-005 | Protect traffic and authenticate paired devices using TLS 1.3 identities. |
| G-006 | Process files and chunks concurrently within explicit resource limits. |
| G-007 | Avoid known-unproductive compression and expose compression as a measurable, configurable policy. |
| G-008 | Adapt transport concurrency without changing logical chunk identity. |
| G-009 | Produce telemetry and reproducible benchmark evidence for every performance claim. |
| G-010 | Remain implementable and operable by one developer without a permanent server or paid service. |

## Non-goals

- A permanent cloud or central coordination server.
- Automatic bidirectional synchronization, merge, or conflict resolution.
- Multi-source/swarm downloading or rarest-chunk-first scheduling.
- Internet NAT traversal, relay service, or automatic firewall configuration.
- Anonymous pairing, public file sharing, accounts, organizations, or per-user ACLs.
- Hiding plaintext from the receiving device or providing application-managed encryption at rest.
- Preserving symlinks, device files, sockets, ACLs, extended attributes, sparse layout, ownership, or platform-specific metadata in v1.
- A GUI, mobile application, or browser client.
- AI/ML-based decisions.

## Scope

V1 provides a C++17 command-line peer application with `init`, `pair`, `receive`, `send`, `peers`, and maintenance/diagnostic operations. A transfer has one sender and one receiver. The sender offers one or more regular files; explicitly selected directories may be recursively enumerated into regular files. The receiver approves the session, determines which logical chunks it already owns, receives missing chunks across a bounded connection pool, reconstructs files in staging, verifies them, and atomically finalizes each accepted file.

Either device may take the sender or receiver role in a later session. A receiving device maintains local trusted-peer metadata, manifests, session state, and a content-addressed plaintext chunk store.

## Constraints

- Linux is the v1 runtime and benchmark platform.
- GCC and Clang are intended compilers; exact minimum versions are operationally selected before CI freeze.
- The implementation language baseline is C++17.
- TCP with TLS 1.3 is the transport.
- First-time pairing requires users to compare the same short authentication string on both devices.
- V1 uses explicit host/address input; automatic discovery is deferred.
- Correctness, safe recovery, and secure peer authentication take priority over throughput.
- No roadmap performance estimate may be presented as measured fact.

## Success criteria

1. A user can pair two devices and send a batch containing multiple regular files.
2. Every completed destination file matches the sender's declared whole-file SHA-256 digest.
3. An interrupted session can reconnect and avoids resending chunks the receiver has already durably verified.
4. Re-sending unchanged or partly changed content transfers only receiver-missing logical chunks plus protocol overhead.
5. An incomplete or corrupt file is never renamed to the final destination.
6. An unpaired peer cannot start a normal transfer, and a changed pinned certificate is rejected.
7. Concurrent sessions remain within configured CPU, memory, storage, connection, and in-flight-byte limits.
8. Compression, scheduling, bytes avoided, retries, and failures are observable without logging file contents or private keys.
9. Benchmark claims include the test configuration, raw results, repetitions, and comparison method.
10. Required automated tests and independent review pass under the Definition of Done.

No fixed speedup or bandwidth-reduction percentage is a v1 acceptance criterion until established by the approved benchmark process.

## Assumptions

- Both devices and their local stores are trusted by the operator.
- Both devices are online and mutually reachable during a transfer.
- Users can compare a short code during first pairing.
- The destination filesystem supports atomic rename within the staging/finalization filesystem.
- Receiver storage capacity is finite and must be checked before accepting work.

If any assumption becomes false, the affected requirements and ADRs must be reopened.
