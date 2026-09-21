# ADR-004 — Hybrid Local Metadata and Chunk Store

- **Status:** Accepted
- **Date:** 2026-09-22

## Context

Resume, deduplication, manifests, and garbage collection require transactional metadata. Potentially large immutable chunk bytes are inefficient to duplicate inside application-defined manifest files and need crash-safe installation. Operating a standalone database conflicts with the no-server local setup.

## Decision

Each receiving device uses:

- SQLite in WAL mode for trusted peers, sessions, files, chunk metadata, ordered references, and durable receipts.
- A local filesystem content-addressed store for verified plaintext chunk payloads.
- Same-filesystem temporary write, file flush, atomic rename, directory durability step, and short SQLite transaction before acknowledging a new chunk.
- Staging files on the same filesystem as their final destination when atomic replacement is required.

SQLite blobs do not hold chunk payloads. Persistent schema changes are versioned migrations. Startup reconciliation repairs or quarantines inconsistencies conservatively.

## Alternatives considered

- **Flat manifest files only:** difficult transactional updates, concurrent access, queries, and garbage collection.
- **All data in SQLite:** simple transaction boundary but poor fit for large streamed payloads and increases database amplification.
- **External database:** unnecessary operations and violates standalone goals.
- **In-memory state:** cannot provide resume after process/power loss.

## Consequences

- Filesystem and database cannot be one atomic transaction; ordered commit and reconciliation are mandatory.
- SQLite write transactions remain short; file I/O occurs outside them.
- Concurrent sessions are supported but metadata writes serialize at SQLite's writer boundary.
- Store directories, DB backups, corruption handling, leases, and GC require documentation/tests.

## Migration implications

Schema migrations must be forward-only for a release and backed up/tested. Changing hash algorithms or encrypting stored chunks requires store migration/versioning and a new ADR.
