# ADR-006 — Configurable Compression Policy

- **Status:** Accepted
- **Date:** 2026-09-22

## Context

Always compressing wastes CPU and can expand already-compressed input. A hardcoded entropy threshold or codec policy would turn an unmeasured convenience into architecture. Peers still need explicit interoperable codecs.

## Decision

Raw, LZ4, and Zstd are the v1 wire representations when built and negotiated. Logical chunk identity always uses plaintext. Operators can select `off`, `lz4`, `zstd`, or `auto`.

`auto` first estimates whether compression is likely to reduce framed bytes. High-entropy candidates are sent raw. For candidates selected for compression, the configured auto-policy chooses LZ4 or Zstd and uses the result only if its encoded payload plus codec metadata is smaller than raw framing.

Entropy sampling, threshold, codec level, and automatic codec-selection policy are experimental/configurable values chosen by benchmark evidence. They are not persisted as architectural invariants; the chosen codec and plaintext length are carried with every chunk attempt.

## Alternatives considered

- **Always Zstd or LZ4:** predictable but wastes work on incompressible chunks.
- **Extension/MIME routing:** filenames are unreliable and can leak policy assumptions.
- **Compress with both codecs for every chunk:** chooses well but doubles CPU work.
- **One hardcoded threshold:** lacks evidence and portability across workloads/hardware.

## Consequences

- LZ4 and Zstd libraries are dependencies unless a build explicitly disables the corresponding advertised capability.
- Auto-policy defaults cannot be frozen until EXP-COMP-001 completes.
- Receiver decompression is bounded by declared logical length and global safety limits.
- Telemetry must distinguish skipped, attempted, accepted, and discarded compression.

## Migration implications

New codecs require capability negotiation and a protocol-minor extension. Removing a codec must not break resume of a session whose manifest is unchanged; the sender can resend the plaintext using another mutually supported representation.
