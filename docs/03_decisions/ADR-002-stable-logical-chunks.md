# ADR-002 — Stable Logical Chunks

- **Status:** Accepted
- **Date:** 2026-09-22

## Context

Delta reuse and crash resume depend on stable chunk identity. The source roadmap also proposed changing “chunk size” as network conditions change. Recomputing CDC boundaries mid-session would invalidate hashes and manifests, defeating reuse and recovery.

## Decision

Rabin CDC produces immutable logical chunks under a versioned profile. A manifest records that profile, boundaries, lengths, and hashes and becomes immutable on acceptance.

Adaptive scheduling may change active data connections, queued/in-flight bytes, retry timing, and chunk work order. Transport framing MAY split an encoded chunk into bounded frames, but this never changes logical chunk identity.

CDC and scheduler parameters are separate configuration namespaces. CDC-profile changes create a new manifest and may reduce reuse with older profiles; scheduler changes do not.

## Alternatives considered

- **Dynamically rechunk active files:** rejected because it breaks manifest identity and resume.
- **Fixed-size logical blocks:** simpler but a byte insertion shifts downstream blocks and reduces delta reuse.
- **One hardcoded CDC profile forever:** rejected because tuning and future compatibility require explicit versioning.

## Consequences

- Protocol and storage distinguish logical chunk length from encoded/frame length.
- The scheduler cannot claim adaptive chunk sizing; it claims adaptive transfer concurrency/windowing.
- CDC default parameters remain an experimental selection before protocol profile `rabin-v1` is frozen.
- Golden vectors are required for cross-build determinism.

## Migration implications

New profiles receive new IDs. Receivers may retain chunks shared by hash across profiles, but manifests are never reinterpreted under another profile.
