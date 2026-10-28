# Data Lifecycle and Recovery State

> Status: Approved

## Ownership

| Data | Owner / authority | Lifetime |
|---|---|---|
| Device private key | Local identity subsystem | Until explicit identity reset; backed up only by operator policy. |
| Trusted-peer pin | Local operator/trust store | Until revocation/removal. |
| Source files | Sender filesystem/user | Never modified by Spoink. |
| Accepted manifest | Receiver session store | Immutable through session retention. |
| Session capability | Receiver session controller | Memory-only; connection/session epoch. |
| Chunk blob | Receiver content store | While referenced/leased or retained by policy. |
| Staging file | Receiver reconstruction | Until finalization, cancellation cleanup, or resumable retention expiry. |
| Final file | Receiver operator | Outside chunk-store GC; deletion only through normal filesystem/user action. |
| Logs/reports | Local operator | Configured retention. |

## Creation and update

### Identity

Identity creation is exclusive. Private key is written to a new file with restrictive permissions, flushed, and atomically installed before the certificate/public metadata. Incomplete identity directories prevent normal operation and require repair/reset guidance.

### Accepted session

After operator approval and full manifest validation, receiver creates the session graph in one SQLite transaction. No chunk payload is accepted before this commit. Accepted manifest bytes and digest never change.

### Chunk installation

New chunks follow the commit sequence in `04_02_components_and_runtime_flows.md`. Temporary names contain random local values, not network paths. Existing canonical chunks are verified by catalog state and periodic integrity policy; their mere filename is insufficient.

### Staging and finalization

Staging is created on the destination filesystem so rename can be atomic. It is opened without following symlinks and with exclusive creation. Reconstruction uses ordered verified chunks. After size/hash verification, staged file data is flushed before rename/replacement, and the affected parent directory is flushed after the rename according to the Linux durability policy.

## Startup reconciliation

The process obtains a single-instance lock for the state root before reconciliation.

1. Validate identity files and database schema/version.
2. Run SQLite integrity checks at the configured depth; a failed mandatory check enters read-only recovery mode.
3. Mark expired leases from prior process instances inactive.
4. Inspect `INSTALLING` catalog entries and chunk temporary files.
5. For a canonical blob without a verified DB row, hash/size-verify it under a bounded reconciliation budget, then adopt or quarantine it.
6. For a `VERIFIED` DB row with missing/wrong blob, mark `QUARANTINED`, invalidate dependent session receipts, and prevent reconstruction.
7. Recompute/validate reference counts when a dirty-shutdown marker or integrity mismatch exists.
8. Inspect staging files against session/file state; resume valid staging only through reconstruction verification, otherwise retain/quarantine per policy.
9. Convert active non-terminal sessions from the prior process to `PAUSED` unless they already have a provable terminal state.
10. Clear the dirty-shutdown marker only after reconciliation completes.

Reconciliation never makes a final file visible based solely on staging metadata.

## Retention

These are configurable policies, not architectural constants:

- completed-session manifest retention;
- paused-session resume window;
- failed/cancelled session diagnostic retention;
- transfer report/log retention;
- unreferenced-chunk grace period;
- quarantine retention.

Default values are `TBD-OPS-RETENTION` and must be selected before a release package. A missing or invalid policy chooses the conservative action: retain data and report the condition.

## Garbage collection

GC has plan and execute phases.

1. Begin a short transaction and select catalog rows with zero recomputed references, no live lease, eligible state, and expired grace period.
2. Claim each candidate with a GC lease/state transition.
3. Commit selection; delete or move blobs outside the transaction.
4. In a new transaction, remove catalog rows whose blob is absent and whose reference/lease conditions still hold.
5. Release claims and emit an auditable summary.

A concurrent new manifest reference either wins before claim or observes the claim and waits/retries. GC cannot delete a claimed chunk that gained a reference.

## Deletion semantics

- Revoking/removing a peer does not delete files, manifests, or chunks automatically.
- Cancelling a session stops transfer and follows retention policy; completed files stay.
- Deleting a session record first removes its file references transactionally, then makes unreferenced chunks eligible after grace.
- Spoink does not provide deletion synchronization in v1.
- Identity reset requires explicit confirmation and does not silently erase received files; it invalidates pairings.

## Backup and restore

- Final received files are backed up by the operator's normal filesystem process.
- A consistent metadata backup uses SQLite's online backup API or a documented stopped-process copy; copying only the main DB file while WAL is active is forbidden.
- Chunk blobs and metadata backup must be coordinated. Restore runs reconciliation before normal service.
- Identity backup is security-sensitive and optional; restoring it preserves peer ID. Losing it requires re-pairing.

## Corruption

Corrupt chunks are quarantined, never served/reused, and invalidate dependent receipts. If the sender is available and the session retained, they may be requested again. SQLite corruption enters recovery mode; automatic destructive recreation is forbidden unless the operator explicitly chooses reset after backup/export attempts.
