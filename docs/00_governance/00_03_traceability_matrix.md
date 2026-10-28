# Traceability Matrix

> Status: Approved baseline; update with every material feature change.  
> Planned implementation paths are targets until the repository contains code.

| Feature | Requirements | Decisions / architecture | Data / contract | Planned implementation | Verification |
|---|---|---|---|---|---|
| Device identity and pairing | FR-001, FR-002, FR-003; NFR-004, NFR-005 | ADR-001, ADR-003; ARCH-01 | DB-001; API-001, API-002; CLI-001, CLI-002 | `src/identity`, `src/pairing`, `src/transport` | TEST-SEC-001–004, TEST-E2E-001 |
| Multi-file direct send | FR-004–007; NFR-001, NFR-008 | ADR-001, ADR-005; ARCH-02, ARCH-03 | DB-002, DB-003; API-003–006; CLI-003, CLI-004 | `src/session`, `src/manifest`, `src/transfer` | TEST-INT-001–004, TEST-E2E-002 |
| CDC and delta reuse | FR-008–010; NFR-002, NFR-009 | ADR-002; ARCH-04 | DB-004, DB-005; API-005, API-006 | `src/chunking`, `src/delta`, `src/store` | TEST-UNIT-CDC-*, TEST-PROP-001, TEST-E2E-003 |
| Compression | FR-011; NFR-003, NFR-012 | ADR-006; ARCH-05 | API-007; CFG-COMP-* | `src/compression` | TEST-UNIT-COMP-*, TEST-INT-005, TEST-PERF-003 |
| Parallel adaptive transfer | FR-012, FR-013; NFR-006, NFR-007 | ADR-002, ADR-005; ARCH-06 | API-007–010; CFG-NET-*, EXP-SCHED-* | `src/scheduler`, `src/transport` | TEST-INT-006, TEST-FAULT-001, TEST-PERF-001–002 |
| Integrity and atomic finalization | FR-014–017; NFR-001, NFR-010 | ADR-004; ARCH-07 | DB-003–006; API-008–011 | `src/store`, `src/reconstruct` | TEST-FAULT-002–006, TEST-E2E-004 |
| Resume and idempotency | FR-018–020; NFR-002 | ADR-004, ADR-005; ARCH-08 | DB-002, DB-006; API-009, API-010 | `src/session`, `src/store`, `src/protocol` | TEST-FAULT-001–005, TEST-E2E-005 |
| Local deduplication | FR-021; NFR-011 | ADR-004; ARCH-09 | DB-004, DB-005 | `src/store`, `src/gc` | TEST-INT-007, TEST-PROP-002 |
| Telemetry and reports | FR-022–024; NFR-013, NFR-014 | ARCH-10 | CLI-005; telemetry schema | `src/telemetry`, `src/cli` | TEST-OBS-001–003, TEST-PERF-* |
| Maintenance and deletion | FR-025–027; NFR-011 | ADR-003, ADR-004; ARCH-09 | DB-001–006; CLI-006 | `src/identity`, `src/gc`, `src/cli` | TEST-SEC-004, TEST-INT-008–009 |

| M0 support and build foundation | M0-001; IC common conventions | ARCH support boundary; `TBD-ENG-DEP-001` | IC checked Result/Error, size, time, ownership conventions | `CMakeLists.txt`, `CMakePresets.json`, `include/spoink/support`, `src/support`, `src/cli/main.cpp` | `tests/unit/*`, format, clang-tidy, GCC/Clang, ASan/UBSan, dependency/secret/docs CI checks |

## Maintenance rule

A feature is not implementation-complete if it has no requirement, ownership boundary, relevant contract/data reference, and verification evidence. A row MAY point to “not applicable,” but that conclusion must be explicit.
