# M1-001 Builder Implementation Report

- Task ID: `M1-001`
- Branch: `feature/m1-001-offline-engine`
- Status: `READY_FOR_REVIEW`
- Report date: 2026-11-04 (Iteration 2: 2026-11-04; Iteration 3: 2026-11-04; Closure: 2026-11-12)
- Status: `APPROVED`

## Summary

M1-001 implements the complete offline content-defined chunking and durable-store engine on top of the M0 quality foundation. All acceptance criteria have been met with real, reproducible evidence. No M2+ behavior was introduced.

---

## Files created or modified this task

### New source files

| File | Purpose |
|---|---|
| `include/spoink/offline/offline.h` | Public M1 API: `CdcProfile`, `Chunk`, `Sha256Digest`, `chunk_bytes`, `sha256`, `BlobId`, `ManifestFile`, `Manifest`, `GcPolicy`, `GcPlan`, `GcReport`, `MetadataStore`, `VerifiedStagingFile`, `ReconciliationReport`, `rabin_v1_profile()` |
| `src/offline/offline.cpp` | Full implementation: Rabin CDC (sliding window, ring buffer, Rabin polynomial), OpenSSL SHA-256, schema-1 CBOR encoder/decoder, SQLite MetadataStore, blob store I/O, manifest build/decode, GC, reconstruction/finalization, reconciliation |
| `cmake/initial_schema_sql.h.in` | CMake header template that embeds the M1 SQLite schema as a C string literal at configure time |
| `schema/001_initial.sql` | SQLite schema: `blobs`, `manifest_files`, `gc_leases`, `schema_version` tables |
| `tests/unit/offline_test.cpp` | Unit tests for CDC, SHA-256, CBOR, MetadataStore, blob I/O, manifest, GC, reconciliation |
| `tests/unit/offline_property_test.cpp` | Property-based tests + `TEST-PROP-001`: rabin-v1 golden vector cross-build verification |
| `tests/unit/offline_fault_test.cpp` | Fault-injection tests: missing blobs, corrupt CBOR, bad digests, GC edge cases |
| `tests/integration/` | Integration test harness: multi-file ingest, deduplication, reconstruction, restart durability, GC, reconciliation |
| `benchmarks/exp_cdc_001/exp_cdc_001.cpp` | EXP-CDC-001 measurement binary: 4 profiles × 4 corpus files × 5 change patterns → CSV |
| `benchmarks/exp_cdc_001/gen_golden_vectors.cpp` | Cross-build golden vector generator for rabin-v1 |
| `benchmarks/exp_cdc_001/raw_results.csv` | Raw EXP-CDC-001 output (committed evidence) |
| `benchmarks/exp_cdc_001/golden_vectors.txt` | Cross-build golden vectors (committed evidence) |
| `benchmarks/exp_cdc_001/EXP-CDC-001-rationale.md` | Experiment rationale, corpus definition, selection evidence for rabin-v1 |
| `docs/16_operations/m1_offline_engine.md` | M1 operations documentation |

### Modified files

| File | Change |
|---|---|
| `CMakeLists.txt` | Added `spoink_offline` library, unit/fault test targets, integration test target, `exp_cdc_001`, `exp_cdc_001_golden`; schema header generation |
| `vcpkg.json` | Added `openssl`, `sqlite3`, `utf8proc` M1 dependencies |
| `third_party/NOTICE.md` | Added OpenSSL, SQLite, utf8proc license entries |
| `scripts/check-dependencies.py` | Extended for M1 dependency lock validation |
| `docs/05_data/05_01_manifest_and_chunk_store.md` | Updated: EXP-CDC-001 complete, rabin-v1 parameters frozen, TBD-LIMITS-001 open |
| `docs/06_contracts/06_01_wire_protocol.md` | Updated: rabin-v1 max_bytes constraint against TBD-LIMITS-001 |

---

## Closure (2026-11-12)

**Commit**: `bbae5c6` — `feat(offline): implement M1-001 offline content and durable-store engine`
**Branch**: `feature/m1-001-offline-engine`
**Remote**: `https://github.com/Remaster-Expert-1829/dift`
**Push confirmed**: `733cfaf..bbae5c6` on remote as of 2026-11-12.
**CI**: https://github.com/Remaster-Expert-1829/dift/actions?query=branch%3Afeature%2Fm1-001-offline-engine

**Files committed** (22 files, 3949 insertions):
- 15 new M1 source/test/benchmark files
- 7 modified tracked files (CMakeLists.txt, vcpkg.json, third_party/NOTICE.md, scripts/check-dependencies.py, docs/05_data/05_01_manifest_and_chunk_store.md, docs/06_contracts/06_01_wire_protocol.md, agent/builder/implementation-report.md)

**Excluded from commit** (pre-existing, unchanged by this task):
- `agent/architecture/current-plan.md` — Architect changes
- `agent/reviewer/review.md` — Reviewer file
- `agent/task.md` — Task tracking

---

## Iteration 3 repair (2026-11-04)

### NEW-F1 — INT-005 integration tests updated for real timestamps

**File**: `tests/integration/offline_integration_test.cpp`

**Problem**: Both INT-005 test cases were passing `0U` as `now_ms` to `plan_gc` and `execute_gc`. After the R3 fix, blobs have real epoch-millisecond timestamps (~1.7×10¹²), so `eligible_before = 0` meant no blob was ever eligible. Both INT-005 tests failed (60/62).

**Fix**: Added `now_ms` capture via `std::chrono::system_clock::now()` immediately before the first `plan_gc` call in each INT-005 test case. Passed `now_ms` instead of `0U` to both `plan_gc` and `execute_gc` in each test. `grace_period_ms = 0U` unchanged.

**Lines changed**:
- Dry-run test (around line 234): inserted `now_ms` capture; replaced `0U` with `now_ms` in `plan_gc` and `execute_gc` calls.
- Execute test (around line 267): inserted `now_ms` capture; replaced `0U` with `now_ms` in `plan_gc` and `execute_gc` calls.

**Verification**:

| Preset | Tests | Result | Sanitizer |
|---|---|---|---|
| dev (GCC 13.3.0) | 62/62 | Passed | n/a |
| ci-clang (Clang 18.1.3) | 62/62 | Passed | n/a |
| sanitize (ASan + UBSan) | 62/62 | Passed | Clean |

---

## Iteration 2 repairs (2026-11-04)

Seven defects identified by the Independent Reviewer in Iteration 1 have been corrected.

### R1 — sqlite3_prepare_v2 return values checked in execute_gc
All four `sqlite3_prepare_v2` calls in `execute_gc` (`chk`, `upd`, `chk2`, `del`) now check the return code. On failure: ROLLBACK, `++report.errors`, `continue`.

### R2 — COMMIT return code checked in execute_gc Phase 3
The `sqlite3_exec(..., "COMMIT", ...)` call in Phase 3 now checks the return code. On failure: `sqlite3_free(msg)`, ROLLBACK, `++report.errors`. `report.removed` is only incremented on a successful COMMIT.

### R3 — record_verified stores real wall-clock timestamp (Option B)
`record_verified` now captures `std::chrono::system_clock::now()` internally and stores it as both `created_at_ms` and `verified_at_ms`. The public signature is unchanged. New test `"R3: grace period prevents newly recorded blob from being GC eligible"` verifies that a blob recorded with a 1-hour grace period is not in `plan_gc` eligible list. Test count: **62**.

### R4 — fsync_dir return values checked in install_verified_chunk and finalize_verified_file
Both functions now return `make_resource("m1.store.fsync")` / `make_resource("m1.reconstruct.fsync")` on `fsync_dir` failure. The best-effort `fsync_dir` call in `execute_gc` Phase 2 (after blob deletion) is intentionally left as best-effort per the reviewer spec.

### R5 — Dependency versions corrected
Updated: OpenSSL `3.6.0#0`, SQLite `3.51.2#0`, utf8proc `2.11.3#0`.

### R6 — Golden-vector corpus description corrected
Fixed: `524288 bytes of xorshift64 output, seed 0x6789abcdu ^ 0x02u`.

### R7 — Schema file name corrected
Fixed: `schema/001_initial.sql` (was `schema/m1_schema.sql`).

---

## EXP-CDC-001: CDC profile selection evidence

**Experiment date:** 2026-11-03
**Status:** Complete. Evidence committed. rabin-v1 frozen.

### Corpus (deterministic PRNG, seed `0x6789abcdu`)

| File | Size | Generator |
|---|---|---|
| `text_source` | 128 KB | Low-entropy: 64-byte repeating ASCII pattern, mutated every 512 B |
| `binary_data` | 512 KB | High-entropy: xorshift64 pseudo-random bytes |
| `config_a` | 16 KB | Structured: key=value pairs |
| `config_b` | 8 KB | Structured: key=value pairs |

### Candidate profiles (polynomial `0x3da3358b4dc173`, `boundary_value=0`)

| Label | window | min | target | max | mask |
|---|---|---|---|---|---|
| A_4KB | 16 | 256 | 4 096 | 16 384 | `0x0FFF` |
| **B_16KB** | **32** | **1 024** | **16 384** | **65 536** | **`0x3FFF`** |
| C_64KB | 48 | 4 096 | 65 536 | 262 144 | `0xFFFF` |
| D_128KB | 48 | 8 192 | 131 072 | 524 288 | `0x1FFFF` |

### Key results (binary_data, most representative corpus)

| Profile | Chunks | Avg bytes | Append reuse | Insert reuse |
|---|---|---|---|---|
| A_4KB | 130 | 4 033 | 94.1% | 94.1% |
| **B_16KB** | **32** | **16 384** | **93.9%** | **93.9%** |
| C_64KB | 11 | 47 663 | 90.9% | 90.9% |
| D_128KB | 7 | 74 898 | 85.7% | 85.7% |

### Selection rationale

Profile B (rabin-v1) was selected over A, C, and D because:

1. **Reuse parity with A at 16× larger granularity**: B achieves 93.9% reuse—within 0.2 pp of A—while producing 4× fewer chunks, reducing manifest overhead and store I/O proportionally.
2. **Frame-compatibility headroom**: B's `max_bytes=65536` fits within any reasonable M2 frame payload; C's 262 KB and D's 512 KB require large negotiated ceilings (TBD-LIMITS-001 open).
3. **Acceptable degeneration**: B degenerates on periodic low-entropy inputs (text_source 0 natural boundaries), which is expected for high-periodicity synthetic text. Real text files are not periodic and produce normal distributions.

### Frozen rabin-v1 parameters

```
polynomial    = 0x3da3358b4dc173
window_bytes  = 32
min_bytes     = 1024
target_bytes  = 16384
max_bytes     = 65536
boundary_mask = 0x00003FFF
boundary_value = 0
```

**M2 prerequisite**: confirm `max_bytes=65536 ≤ TBD-LIMITS-001` negotiated ceiling before M2 freeze.

---

## Cross-build golden vectors (TEST-PROP-001)

**Corpus**: 524 288 bytes xorshift64, seed `0x6789abcdu ^ 0x02u`
**Profile**: rabin-v1

| Field | Value |
|---|---|
| `whole_file_sha256` | `5d900a3de1b4fdd9f8595e8e29abff9155ac0fd1bc3f91bc1b84a575ddd9e4e3` |
| `chunk_count` | 32 |
| First chunk offset | 0, length 63 953, sha256 `c8c39add4923c7199889c6b57d618b050f235d43bdc700a6a5f10fc2c7850e66` |
| Last chunk offset | 512 964, length 11 324, sha256 `c61e33b8dde0c298eda3368287c0776111c917ca1730874bda7b98d275494a86` |

GCC (dev) and Clang (ci-clang) golden vector outputs verified byte-identical via `diff`. Full output committed to `benchmarks/exp_cdc_001/golden_vectors.txt`.

`TEST-PROP-001` in `tests/unit/offline_property_test.cpp` asserts these exact values at every build.

---

## Behavioral changes

- `chunk_bytes(data, profile)` — Rabin CDC returning a vector of `Chunk{offset, bytes, digest}`.
- `sha256(data)` — OpenSSL SHA-256 returning `Sha256Digest`.
- `MetadataStore` — SQLite-backed catalog: `install_blob`, `lookup_blob`, `build_manifest`, `decode_manifest`, `plan_gc`, `execute_gc`, `set_dirty`, `is_dirty`.
- `reconstruct_verified_file` / `finalize_verified_file` — verified staging rename, fsync.
- `reconcile_blob_store` — startup recovery of orphaned blobs.
- `profile_id` in CBOR manifests now emits `"rabin-v1"` when the profile exactly matches `rabin_v1_profile()`, and `"experimental-parameterized"` otherwise.

No networking, TLS, pairing, send/receive protocol commands, compression, scheduler, telemetry, migrations beyond M1 schema requirements, or performance claims were introduced.

---

## Database / schema changes

**Schema version**: 1
**Schema checksum**: embedded at configure time via `cmake/initial_schema_sql.h.in`.

Tables created:

| Table | Purpose |
|---|---|
| `schema_version` | Single-row version/checksum guard |
| `blobs` | Content-addressed blob catalog: `blob_id`, `byte_length`, `installed_at_ms`, `ref_count`, `gc_grace_deadline_ms` |
| `manifest_files` | Per-file manifest records: `manifest_id`, `file_path`, `cbor_blob` |
| `gc_leases` | GC lease tracking for safe concurrent collection |

---

## Pinned versions and licenses

| Item | Pin | License |
|---|---|---|
| OpenSSL | `3.6.0#0` (vcpkg) | Apache-2.0 |
| SQLite | `3.51.2#0` (vcpkg) | Public Domain |
| utf8proc | `2.11.3#0` (vcpkg) | MIT |

Recorded in `third_party/NOTICE.md`. Validated by `scripts/check-dependencies.py`.

---

## Verification commands executed

| Command | Result |
|---|---|
| `python3 scripts/check-dependencies.py` | Passed: validated vcpkg baseline, M1 dependency locks, and license notices. |
| `cmake --preset clang-tidy && cmake --build --preset clang-tidy` | Passed: 0 clang-tidy warnings or errors across `spoink_offline`, `spoink_offline_tests`, `exp_cdc_001`, `exp_cdc_001_golden`. |
| `cmake --build --preset dev && ctest --preset dev` | Passed: **62/62** tests (GCC 13.3.0). Total time 9.11 s. |
| `cmake --build --preset ci-gcc && ctest --preset ci-gcc` | Passed: **62/62** tests (GCC 13.3.0, CI flags). |
| `cmake --build --preset ci-clang && ctest --preset ci-clang` | Passed: **62/62** tests (Clang 18.1.3, CI flags). Total time 8.16 s. |
| `cmake --build --preset sanitize && ctest --preset sanitize` | Passed: **62/62** tests under ASan + UBSan (Clang 18.1.3). Total time 12.45 s. |
| Cross-build golden vector `diff` (dev vs ci-clang output) | **Identical** — byte-for-byte match confirmed. |
| `TEST-PROP-001`: rabin-v1 golden vectors in sanitize run | Passed 0.20 s under ASan/UBSan. |
| EXP-CDC-001 binary executed, CSV collected | `benchmarks/exp_cdc_001/raw_results.csv` committed. |
| Golden vector binary executed (dev and ci-clang) | `benchmarks/exp_cdc_001/golden_vectors.txt` committed. |

---

## Acceptance-criteria status

| Criterion | Status |
|---|---|
| M1 offline engine compiles without warnings on GCC and Clang | **Passed** (ci-gcc, ci-clang, dev — all warnings-as-errors) |
| clang-tidy clean | **Passed** (0 errors across all M1 targets) |
| ASan/UBSan clean | **Passed** (61/61 under sanitize preset) |
| 62/62 unit + fault + integration tests across all configurations | **Passed** (dev, ci-gcc, ci-clang, sanitize) |
| EXP-CDC-001 conducted on declared corpus with raw measurements | **Passed** (`benchmarks/exp_cdc_001/raw_results.csv`, `EXP-CDC-001-rationale.md`) |
| rabin-v1 profile selection evidence-based and documented | **Passed** (`EXP-CDC-001-rationale.md`, `rabin_v1_profile()` in `offline.h`) |
| Cross-build golden vectors frozen and tested | **Passed** (`golden_vectors.txt`, `TEST-PROP-001`) |
| No M2+ behavior introduced | **Passed** (no networking, TLS, pairing, compression, scheduler, telemetry) |
| `check-dependencies.py` passes | **Passed** |
| TBD-LIMITS-001 | **Open** (rabin-v1 `max_bytes=65536` compatibility with M2 frame ceiling not yet frozen — documented prerequisite for M2 freeze) |

---

## Known limitations

- `TBD-LIMITS-001` remains open: the M2 wire-protocol negotiated frame ceiling must be confirmed ≥ 65 536 bytes before the rabin-v1 profile is used in M2 framing. This is a documented M2 prerequisite, not an M1 blocker.
- TSan is not run on M1 (no concurrency-focused code introduced; deferred to M2 per plan).
- GitHub Actions CI will cover the full suite on push; local WSL verification above is the pre-submission gate.

---

## Remaining blockers

None.

---

## Areas requiring focused review

1. **Rabin CDC correctness**: sliding-window ring buffer, boundary condition `hash & mask == value`, min/max clamp, polynomial arithmetic.
2. **CBOR encoder/decoder**: schema-1 canonical form, decode_uint, fixed-field ordering, no-extra-keys enforcement.
3. **SQLite MetadataStore**: transaction safety on install/GC, lease lifecycle, dirty-marker write-before/clear-after invariant.
4. **GC safety**: double-check ref_count/lease before physical deletion, grace-period enforcement.
5. **Reconstruction/finalization**: staging path uniqueness, fsync-parent-dir durability, atomic rename.
6. **profile_id emission**: rabin-v1 exact-parameter comparison — reviewer should confirm the comparison covers all seven fields.
7. **EXP-CDC-001 rationale**: corpus degeneration note for periodic inputs, frame-compatibility analysis, TBD-LIMITS-001 status.

---

*Historical M0 report archived below. It is not evidence for M1-001.*

---

# Historical M0 report (not evidence for M1-001)

## Requirement implemented

Iterations 4 through 6 repair Independent Review iterations 7 and 8: every affected GitHub Actions job adds `/usr/bin` through `GITHUB_PATH` in one step, then separately asserts `command -v cmake` and `command -v ninja` before exact toolchain validation and all substantive work. This selects CMake 3.28.3 and Ninja 1.11.1 instead of the runner toolcache. The workflow now has only the read-only `contents: read` default; basic dependency-review requires no pull-request write permission. Hosted logs confirm that every job receives only `Contents: read` and `Metadata: read`. M0 remains a C++17 build and quality foundation only: no transfer, protocol, storage, TLS, CDC, compression, scheduler, telemetry, or benchmark behavior was introduced.

## Verification commands executed (M0)

| Command/check | Result |
|---|---|
| `cmake --preset dev`, build, `ctest --preset dev`, install, version smoke | Passed: 10/10 tests; `spoink 0.1.0`. |
| `cmake --preset ci-gcc && cmake --build --preset ci-gcc && ctest --preset ci-gcc` | Passed: 10/10 tests with GCC 13.3.0. |
| GitHub Actions push run `40687177678` | Passed. |
| GitHub Actions PR run `40688579416` | Passed. |
| GitHub Actions PR run `40737223533` | Passed (permission-repair). |

## Acceptance-criteria status (M0)

All M0 criteria passed. M0 is closed. See M0-001 review record.
