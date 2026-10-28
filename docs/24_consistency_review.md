# Documentation Consistency Review

> Status: Completed for initial corpus on 2026-09-22  
> Result: No known contradiction; explicit release conditions remain.

## Review matrix

| Topic | Potential conflict from source/planning | Resolution across corpus | Status |
|---|---|---|---|
| Project rename | Earlier corpus and protocol identifiers used the former project name. | Product name is `Spoink`; executable is `spoink`; environment prefix is `SPOINK_*`; ALPN labels are `spoink/1` and `spoink-pair/1`; wire magic is `SPNK`; staging/include paths use `spoink`. | Resolved |
| Permanent server vs ShareIt-style transfer | Original roadmap used client/server/global server store. | ADR-001: no permanent server; one peer temporarily receives; local store per receiving device. All requirements/architecture/operations use sender/receiver roles. | Resolved |
| Upload vs download | Earlier question assumed a server. | Either peer may send in a later session. V1 has explicit send/receive, not automatic sync or pull from an always-on server. | Resolved |
| File scope | “Multiple files” was approved; original examples focused on one file. | FR-005 and PR-02 define multiple regular files/directories as selection, excluding special metadata/types. | Resolved |
| Adaptive chunk size vs delta/resume | Roadmap proposed changing chunk size during transfer. | ADR-002 fixes logical CDC chunks; scheduler adapts connections/in-flight window only. | Resolved |
| AES-GCM chunks vs authenticated transport | Roadmap proposed custom per-chunk encryption without keys/auth. | ADR-003 uses TLS 1.3 mTLS; custom chunk crypto removed; plaintext-at-rest limitation explicit. | Resolved |
| Cross-transfer “global” dedup privacy | Central/global store conflicted with direct trusted peers. | Deduplication is local per receiving device/trusted namespace; no cross-user/device lookup. | Resolved |
| Parallel threads vs connection semantics | Roadmap did not define whether threads share TCP. | ADR-005 defines one control plus bounded data pool; implementation threads remain private detail. | Resolved |
| Resume-state owner | Roadmap alternated client/server manifests. | Receiver durable state/receipts are authoritative; sender retains source manifest mapping and requests resume. | Resolved |
| Packet-loss estimation over TCP | Roadmap treated ACK gaps as loss. | Scheduler records app RTT, ACK latency, timeouts/errors; shaped loss is benchmark condition, not claimed direct signal. | Resolved |
| Rarest-chunk-first without swarm | USP required multi-source peer availability absent elsewhere. | Multi-source/swarm and rarest-first are explicit non-goals/deferred in ADR-001/charter. | Resolved |
| Benchmark target inconsistencies | 40%, 95%, ≥2×, and sample MB/s mixed targets/results. | All are unverified hypotheses; methodology defines measurements, baselines, and claim rules; no numeric release target. | Resolved |
| rsync characterization | Source stated a simplistic fixed-block insertion failure. | Benchmark document treats rsync as a relevant delta baseline and forbids unsupported claims. | Resolved |
| Manifest representation | Data/contracts needed stable canonical bytes. | ADR-007 and data/wire docs consistently use deterministic CBOR and SHA-256 digest. | Resolved |
| Chunk persistence/ACK | Blob+DB cannot be one atomic transaction. | ADR-004 and runtime/reliability docs define ordered commit, ACK last, and reconciliation. | Resolved |
| Existing destination/batch atomicity | Product had no overwrite/partial rule. | Default reject, explicit overwrite, atomic per-file finalization, batch partial result. | Resolved |
| Compression choice | Source alternated LZ4/Zstd based on vague entropy. | ADR-006: modes/capabilities fixed; auto parameters evidence-driven; use encoded data only when framed size is smaller. | Resolved |
| Source-of-truth hierarchy | Roadmap folder layout/schedule could be mistaken as binding. | Governance declares approved docs/ADRs/contracts authoritative; layout is guidance and schedule is sequencing only. | Resolved |

## Cross-document identifier check

- Requirements `FR-001–027` and `NFR-001–018` appear in requirements and are represented by feature rows in traceability.
- ADR index matches ADR-001–007 and their Accepted status/date.
- Architecture components `ARCH-01–10` map to responsibilities and traceability.
- Data entities `DB-000–007` support identity/trust, sessions, files, chunks, receipts, and leases.
- Contracts `API-001–011`, `CLI-001–006`, and `IC-001–012` cover externally important operations.
- Verification families map to traceability and milestone exits.
- Risks cover the material design/security/measurement concerns without generic filler.

| M0 dependency mechanism | Engineering standards preferred vcpkg but did not select a concrete baseline. | M0 locks vcpkg `2025.10.17` / `74e6536215718009aae747d86d84b78376bf9e09` and Catch2 `3.11.0#0`; lock, notices, bootstrap, presets, and operations instructions agree. | Resolved in M0 |

## Open conditions, not contradictions

| ID | Condition | Owner / resolution |
|---|---|---|
| EXP-CDC-001 | Select and freeze `rabin-v1` parameters/golden vectors. | Builder/Reviewer in M1 via CDC experiment. |
| TBD-SEC-KEY-001 | Select permitted device key algorithm/strength supported by target TLS stack. | Architect/security review before M2. |
| TBD-LIMITS-001 | Select default and absolute parser/resource/timeout ceilings. | Architect/Builder before M2 protocol freeze, using safety analysis/tests. |
| EXP-COMP-001 | Select conservative auto compression threshold/policy. | M4 benchmark training/validation split. |
| EXP-SCHED-001–006 | Select adaptive profile values. | M4 simulation/shaped-network evidence. |
| TBD-OPS-RETENTION | Choose release retention/grace defaults. | M5 capacity/recovery tests and operator documentation. |

These items are explicitly classified and have resolution gates; no document treats their unknown value as fixed.

## Review conclusion

The corpus consistently defines a direct paired-peer system with stable logical chunks, TLS-secured bounded transport, receiver-authoritative durable resume, local deduplication, and evidence-driven performance policy. Re-run this review after any ADR, protocol/schema, or product-scope change and before release.
