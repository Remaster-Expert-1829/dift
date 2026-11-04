# M1 Offline Engine Notes

> Status: M1 implementation record; reviewed 2026-10-30.

M1 operates only on local files. It has no listener, peer, pairing, TLS, send,
receive, protocol framing, compression, or scheduling command.

The local state root contains `metadata.sqlite3` (WAL mode) and `chunks/aa/bb/`
plaintext SHA-256 blobs. State and chunk roots must be local filesystems. A
chunk is considered present only after its plaintext SHA-256 and size are
verified; a filename alone is never evidence of integrity. The implementation
uses a parameterized CDC profile and records all profile values in the manifest.
`EXP-CDC-001` (2026-11-03) selected and froze `rabin-v1`
(polynomial=0x3da3358b4dc173, window=32, min=1024, target=16384, max=65536,
mask=0x3FFF, value=0) after measuring chunk distribution, edit-reuse ratios,
CPU cost, and M2 frame-compatibility analysis. Cross-build golden vectors are
verified in TEST-PROP-001; raw evidence is in `benchmarks/exp_cdc_001/`.
TBD-LIMITS-001 (max_bytes=65536 vs M2 negotiated frame ceiling) remains open.

Schema 1 is created from `schema/001_initial.sql`, embedded into the build so
the executable and checked-in migration cannot silently diverge. It creates the
schema-migration, peer/session/file, chunk-catalog, reference, progress, and
lease tables; chunk payloads remain outside SQLite. An absent retention policy
retains data: this milestone does not choose an M5 release GC default.

The documented fresh-checkout commands remain the M0 commands in
`16_01_local_development_and_deployment.md`; M1 adds OpenSSL, SQLite, and
utf8proc through the pinned vcpkg manifest rather than system packages.
