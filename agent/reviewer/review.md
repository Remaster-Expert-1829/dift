# Independent Review

- Task ID: M1-001
- Builder iteration reviewed: 5
- Review iteration: 5
- Reviewed branch: `feature/m1-001-offline-engine`
- Reviewed HEAD: `bd13bcb` (style: apply clang-format-18 to M1 offline engine files)
- PR: [#4](https://github.com/Remaster-Expert-1829/dift/pull/4)
- CI run: [#14](https://github.com/Remaster-Expert-1829/dift/actions/runs/43198173216)
- Verdict: **APPROVED**

---

## Commit verification

Four commits on `feature/m1-001-offline-engine`:

```
bd13bcb style: apply clang-format-18 to M1 offline engine files
4622aea docs: record M1-001 agent handoffs and reviewer closure
4b16e72 feat(offline): implement M1-001 offline content and durable-store engine
733cfaf docs: prepare M1 offline engine task
```

`bd13bcb` is a formatting-only fix (15 insertions / 19 deletions across 4 files); no logic
change. The M1 implementation commit `4b16e72` (22 files, 3949 insertions) remains
unchanged from Iteration 4.

---

## Hosted CI — run #14

CI run #14 triggered on PR #4 `synchronize` event (push of `bd13bcb`).

| Job | Result |
|---|---|
| `repository-checks` | ✓ Succeeded |
| `build-and-test (ci-gcc)` | ✓ Succeeded |
| `build-and-test (ci-clang)` | ✓ Succeeded |
| `build-and-test (release)` | ✓ Succeeded |
| `clang-tidy` | ✓ Succeeded |
| `asan-ubsan` | ✓ Succeeded |
| `secrets` | ✓ Succeeded |
| `dependency-review` | ✓ Succeeded |

**All 8 jobs passed.** No warnings promoted to errors; Node.js 20 deprecation notice on
`Complete job` is a runner-level warning, not a build failure.

---

## Prior findings — final status

| ID | Description | Status |
|---|---|---|
| F1 | `sqlite3_prepare_v2` return codes unchecked in `execute_gc` | Resolved (R1) |
| F2 | COMMIT return code unchecked; `removed` counter over-incremented | Resolved (R2) |
| F3 | `record_verified` stored zero as `created_at_ms`; grace period broken | Resolved (R3) |
| F4 | `fsync_dir` return ignored in `install_verified_chunk` / `finalize_verified_file` | Resolved (R4) |
| F5 | `plan_gc` SQL used `>` instead of `>=` for `created_at_ms` | Resolved (R5) |
| F6 | No unit test for grace-period correctness | Resolved (R6) |
| F7 | Missing `<chrono>` include in test | Resolved (R7) |
| NEW-F1 | INT-005 passed `now_ms=0U` after R3 made timestamps real | Resolved (Iter 3) |
| CI-F1 | clang-format-18 violations in 4 files | Resolved (Iter 5) |

---

## Final assessment

**APPROVED.** All acceptance criteria for M1-001 are satisfied:

1. Rabin CDC with sliding-window ring buffer — implemented and tested ✓
2. OpenSSL SHA-256 (EVP_Digest) content addressing — implemented ✓
3. Schema-1 canonical CBOR manifest (RFC 8949 deterministic) — implemented and tested ✓
4. SQLite MetadataStore with WAL, FK, two-phase GC with grace period — implemented ✓
5. Content-addressed blob store with fsync durability and atomic rename — implemented ✓
6. 62/62 tests passing (unit + property + fault + integration) — verified locally ✓
7. ASan/UBSan clean — verified locally and by hosted `asan-ubsan` job ✓
8. clang-tidy clean — verified by hosted `clang-tidy` job ✓
9. All 8 hosted CI jobs pass on PR #4 run #14 — verified ✓

M1-001 is **CLOSED**.

---

## Iteration history

- **Iteration 1**: CHANGES_REQUIRED — 7 findings (F1–F7).
- **Iteration 2**: CHANGES_REQUIRED — F1–F7 resolved; NEW-F1 introduced.
- **Iteration 3**: APPROVED (local) — NEW-F1 resolved; 62/62 verified.
- **Iteration 4**: BLOCKED — commit correct, no hosted CI run (feature branch push doesn't trigger workflow).
- **Iteration 5** (this document): **APPROVED** — PR #4 opened; all 8 CI jobs pass on run #14.

---

## M2-001

- Task ID: M2-001
- Review iteration: 1
- Reviewed branch: `feature/m2-001-secure-transfer`
- Reviewed HEAD: `1657e7d` (fix(m2): suppress EnumCastOutOfRange on intentional unknown-type test)
- PR: [#5](https://github.com/Remaster-Expert-1829/dift/pull/5)
- CI run: [43476826623](https://github.com/Remaster-Expert-1829/dift/actions/runs/43476826623)
- Verdict: **CHANGES_REQUIRED**

---

### Commit log (feature/m2-001-secure-transfer above main)

```
1657e7d fix(m2): suppress EnumCastOutOfRange on intentional unknown-type test
0a0d4a8 fix(m2): fix clang-tidy warnings in test files (round 3)
1d48442 fix(m2): fix remaining clang-tidy warnings (round 2)
71fa39b fix(m2): resolve clang-format and clang-tidy CI failures
f8e22ab feat(m2): implement M2-001 secure direct-transfer engine (Slices 1-6)
```

Five commits; `f8e22ab` is the primary implementation (45 files, ~4700 insertions).
Subsequent commits are style/linter fixes only.

---

### Hosted CI — run 43476826623

| Job | Result |
|---|---|
| `repository-checks` | ✓ pass |
| `build-and-test (ci-gcc)` | ✓ pass |
| `build-and-test (ci-clang)` | ✓ pass |
| `build-and-test (release)` | ✓ pass |
| `clang-tidy` | ✓ pass |
| `asan-ubsan` | ✓ pass |
| `secrets` | ✓ pass |
| `dependency-review` | ✓ pass |

**All 8 jobs passed.**

---

### Verification summary — what passes

**Part 1 — Identity and TLS**

- Ed25519 exclusivity: `generate_ed25519_key()` uses `EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr)`;
  `load_private_key()` rejects non-Ed25519 via `EVP_PKEY_get_id(pkey) != EVP_PKEY_ED25519`. ✓
- Key permissions: private key written with `open(O_WRONLY|O_CREAT|O_TRUNC, 0600)`;
  `verify_key_permissions()` checks `(sb.st_mode & 0177U) != 0U`; called BEFORE `load_private_key()`. ✓
- TLS 1.3 only: `SSL_CTX_set_min_proto_version(TLS1_3_VERSION)` and `set_max_proto_version`. ✓
- 0-RTT disabled: `SSL_CTX_set_max_early_data(ctx, 0)`. ✓
- Ed25519-only sigalgs: `SSL_CTX_set1_sigalgs_list(ctx, "ed25519")`. ✓
- ALPN enforcement: unknown ALPN rejected via `make_invalid("m2.identity.alpn_unknown")`; test exists. ✓
- mTLS: both `tls_connect()` and `tls_accept_one()` call `make_tls_context("spoink/1", true)`. ✓
- SPKI pin: `verify_spki_pin()` uses `CRYPTO_memcmp` (constant-time); connection rejected on mismatch. ✓

**Part 3 — Wire protocol**

- 32-byte prefix layout: `magic(4)+major(1)+minor(1)+channel(1)+msg_type(1)+flags(2)+reserved(2)+header_length(4)+payload_length(8)+request_id(8)` = 32. ✓
- Length validation before allocation: `validate_prefix_lengths()` called before body `std::vector<std::byte>` allocation. ✓
- Partial-read state machine: `FrameReader` with `kNeedPrefix→kNeedHeader→kNeedPayload→kComplete`; byte-by-byte test passes. ✓
- CBOR: deterministic integer encoding (minimal width), fixed-count maps, skip() handles forward compatibility. ✓

**Part 4 — State machine**

- Pure: `process(Input)` returns `std::vector<Effect>`, no I/O. ✓
- HELLO→kReady, PING→PONG without state change, kReady→kOfferPending. ✓
- One-session binding: second SESSION_OFFER → protocol_error → kError. ✓
- ACK ordering: `handle_chunk_put()` emits `EffChunkReceived` only; `handle_chunk_stored()` emits `EffSendFrame(CHUNK_ACK)`. ✓
- Unknown message type → kError. ✓

**Part 5 — Pairing**

- SAS derivation: `make_exporter_context()` uses canonical min/max ordering of IDs and nonces; `SSL_export_keying_material("EXPORTER-Spoink-Pairing-v1", ...)`; RAND_bytes for attempt nonce. ✓
- Revocation: `revoke_peer()` sets `revoked_at_ms`; `is_trusted_peer()` checks `revoked_at_ms IS NULL`. ✓
- Single-attempt enforcement: `listen(1)` on the server socket; one accept, one exchange, then close. ✓

**Part 10 — CI**

All 8 jobs pass. ✓

---

### Findings

#### F1 — CRITICAL: `session_capability` sent as all-zeros

**Location:** `src/cli/main.cpp:363`

```cpp
plan.session_capability = {};   // ← zero-filled, not RAND_bytes
```

`session_capability` is the 32-byte bearer token the receiver puts in `TransferPlanMsg` and
the sender echoes in `DataBindMsg` to prove its data connection belongs to this session.
Sending all zeros means any node that can open a TLS connection can forge a valid
`DataBindMsg` and hijack the data channel for any active transfer.

**Required fix:** generate with `RAND_bytes` before sending `TransferPlanMsg`; verify the
echoed value matches before accepting `DataBindMsg`.

---

#### F2 — CRITICAL: Durable-ACK contract violated — ACK sent even when chunk store fails

**Location:** `src/cli/main.cpp:395–416`

```cpp
auto install_r = spoink::offline::install_verified_chunk(srk, chunk_obj);
if (install_r.has_value()) {
    (void)meta.record_verified(digest, ...);
}
// ChunkStored is submitted unconditionally ↓
spoink::session::ChunkStored cs{};
auto ack_effects = sm.process(cs);   // → emits CHUNK_ACK
```

If `install_verified_chunk` fails, `ChunkStored` is still fed to the state machine and
`CHUNK_ACK` is emitted and sent.  The sender concludes the chunk is durable; the receiver
has not stored it.  On reconnect the file cannot be reconstructed and the transfer silently
produces corrupt output.

**Required fix:** only submit `ChunkStored` (and therefore send `CHUNK_ACK`) when
`install_r.has_value()`.  On failure, send `CHUNK_NACK` or close the connection.

---

#### F3 — HIGH: Missing required test coverage

Acceptance criterion 6 in `agent/tasks/M2-001.md` requires:
> TEST-SEC-001–007, parser fuzz corpus, TEST-INT-001/004, TEST-E2E-001/002/004 single-file subset.

What is missing:
- `tests/security/` — no directory; TEST-SEC-001 (MITM simulation) absent.
- `tests/fuzz/` — no directory; parser fuzz corpus absent.
- `tests/integration/m2_e2e_test.cpp` — referenced in `m2_integration_test.cpp` comment as
  "Full E2E (pair + transfer) is in m2_e2e_test.cpp" but file does not exist.
- No non-Ed25519 key rejection test in `identity_test.cpp` (Part 1.1 requirement).

---

#### F4 — MEDIUM: `kMaxFilesPerSession = 10000` does not match M2 operational ceiling

**Location:** `include/spoink/protocol/framing.h:19`

```cpp
static constexpr std::uint32_t kMaxFilesPerSession = 10000U;
```

M2-001 scope is a single-file transfer.  The task (`agent/tasks/M2-001.md` §8) requires
"per-session file/chunk counts for single-file scope".  The constant expresses the
protocol-major-1 ceiling, but no enforcement anywhere rejects a session offering more than 1
file for M2.  The state machine accepts any `file_count`.  The mandate requires the M2
operational value be enforced.

**Required fix:** enforce `file_count == 1` in `handle_session_offer()` for M2 (or document
that this ceiling is intentionally deferred to M3 with explicit scope note).

---

#### F5 — LOW: No `static_assert` that `kMaxPayloadBytes` covers rabin-v1 max chunk size

**Location:** `include/spoink/protocol/framing.h`

The task §8 requires confirmation that `kMaxPayloadBytes` (131072) ≥ rabin-v1 max_bytes
(65536).  The relationship is correct numerically, but no `static_assert` codifies it.
If either constant changes, the invariant silently breaks.

**Required fix:** add `static_assert(Limits::kMaxPayloadBytes >= 65536U, ...)` near the
rabin-v1 max chunk constant.

---

### Assessment

The security architecture (Ed25519 identity, TLS 1.3, 0-RTT disabled, SPKI pinning,
constant-time comparison, permission checks, SAS derivation) is well-implemented and matches
the design intent.  The state machine is clean and pure.  Frame parsing with pre-allocation
length checks is correct.

However, two correctness/security violations in the CLI integration layer (F1 and F2) mean
the protocol's security and durability guarantees are not upheld end-to-end.  Three
acceptance criteria from `agent/tasks/M2-001.md` are unmet (F3, F4, F5).

**CHANGES_REQUIRED.** Address F1 and F2 as blockers before merge.

---

## M2-001 Iteration 2

- Task ID: M2-001
- Review iteration: 2
- Reviewed branch: `feature/m2-001-secure-transfer`
- Reviewed HEAD: `a5a1751` (docs(m2): update implementation report — Iteration 2 findings addressed)
- PR: [#5](https://github.com/Remaster-Expert-1829/dift/pull/5)
- CI run: [43720963999](https://github.com/Remaster-Expert-1829/dift/actions/runs/43720963999)
- Verdict: **CHANGES_REQUIRED**

---

### Commit log (Iteration 2 above Iteration 1 head `ca7e8f0`)

```
a5a1751 docs(m2): update implementation report — Iteration 2 findings addressed
379361d fix(m2): address reviewer iteration 1 findings (F1-F5)
```

Two commits; `379361d` is the fix commit (8 files, +815 / -19).  `a5a1751` is a
documentation-only update to `agent/builder/implementation-report.md`.

---

### Hosted CI — run 43720963999

| Job | Result |
|---|---|
| `repository-checks` | ✓ pass |
| `build-and-test (ci-gcc)` | ✓ pass |
| `build-and-test (ci-clang)` | ✓ pass |
| `build-and-test (release)` | ✓ pass |
| `clang-tidy` | ✗ **FAIL** |
| `asan-ubsan` | ✓ pass |
| `secrets` | ✓ pass |
| `dependency-review` | ✓ pass |

**7/8 jobs passed.  `clang-tidy` failed.**

---

### Iteration 1 findings — resolution status

| ID | Description | Status |
|---|---|---|
| F1 | `session_capability` sent as all-zeros | **Resolved** |
| F2 | `CHUNK_ACK` sent even when chunk store fails | **Resolved** |
| F3 | Missing required test coverage | **Resolved** |
| F4 | No M2 single-file enforcement in state machine | **Resolved** |
| F5 | No `static_assert` for `kMaxPayloadBytes >= 65536` | **Resolved** |

#### F1 — Verified resolved

`RAND_bytes(session_cap.data(), 32)` populates `plan.session_capability` before
`TransferPlanMsg` is sent (`src/cli/main.cpp:432–440`).  Receiver intercepts
`kDataBind` before the state machine and verifies the echoed capability with
`CRYPTO_memcmp` (`src/cli/main.cpp:349–381`).  Sender sends `DataBind` with the
echoed cap and reads `DataBindResult` synchronously before sending any chunk
(`src/cli/main.cpp:224–261`).  Token is never persisted.  Constant-time
comparison is used throughout. ✓

#### F2 — Verified resolved

`ChunkStored` / `CHUNK_ACK` are only submitted inside the `install_r.has_value()`
branch (`src/cli/main.cpp:471–498`).  The else branch submits `ChunkStoreFailed`
to the state machine, which produces `CHUNK_NACK` sent to the sender, then returns
error 1 (`src/cli/main.cpp:499–514`). ✓

#### F3 — Verified resolved

All four specifically-enumerated missing items are present:

- `tests/security/sec_test.cpp` — TEST-SEC-001: two identities with empty trust
  stores; both `tls_accept_one` and `tls_connect` must fail.  Port 19877. ✓
- `tests/fuzz/fuzz_framing.cpp` — libFuzzer target; feeds arbitrary bytes into
  `FrameReader::feed()` and `decode_prefix()`; enabled with
  `-DSPOINK_ENABLE_FUZZING=ON`. ✓
- `tests/integration/m2_e2e_test.cpp` — TEST-E2E-001: two in-process TLS
  endpoints on loopback (port 19878), simulated pairing via `save_trusted_peer`,
  100-byte file, full state machine protocol run, byte-identical output
  verified. ✓
- Non-Ed25519 key rejection test in `tests/unit/identity_test.cpp` — writes an
  RSA-2048 PEM key to `state_root/identity/device.key`; verifies
  `load_or_create_identity` returns error. ✓

`CMakeLists.txt` registers `spoink_m2_e2e_test`, `spoink_security_tests`, and
the guarded `spoink_fuzz_framing` targets. ✓

#### F4 — Verified resolved

`handle_session_offer()` now rejects `msg.file_count != 1` via
`protocol_error("m2.sm.multi_file_unsupported")`
(`src/session/state_machine.cpp:202–204`).  Comment on `kMaxFilesPerSession`
updated to reference the M2 enforcement point. ✓

#### F5 — Verified resolved

`static_assert(Limits::kMaxPayloadBytes >= 65536U, ...)` added immediately after
the `Limits` struct in `include/spoink/protocol/framing.h:35`.  Actual value is
131072, so the assert passes. ✓

---

### New findings

#### N-F1 — BLOCKER: `clang-tidy` CI job fails on Iteration 2 test files

**CI evidence:** run 43720963999, job `clang-tidy`, `FAILURE`.

Two new test files introduced violations that are promoted to errors by
`WarningsAsErrors: '*'` in `.clang-tidy`.

**`tests/security/sec_test.cpp`** (8 violations):

| Location | Check | Variable |
|---|---|---|
| `:46` | `readability-identifier-naming` | `rootA` |
| `:47` | `readability-identifier-naming` | `rootB` |
| `:51` | `readability-identifier-naming` | `idA_r` |
| `:53` | `readability-identifier-naming` | `idB_r` |
| `:56` | `readability-identifier-naming` | `dbA` |
| `:57` | `readability-identifier-naming` | `dbB` |
| `:66` | `readability-identifier-naming` | `idA` |
| `:86` | `bugprone-unchecked-optional-access` | `*server_rejected` |

The `readability-identifier-naming` violations are because short capitalised
suffixes (`A`, `B`) in variable names do not match the project's snake_case
convention.  The `bugprone-unchecked-optional-access` fires on `*server_rejected`
at line 86 even though `REQUIRE(server_rejected.has_value())` precedes it —
clang-tidy cannot see through the macro.

**`tests/integration/m2_e2e_test.cpp`** (10+ violations):

| Location | Check | Variable |
|---|---|---|
| `:102` | `readability-identifier-naming` | `rootS` |
| `:103` | `readability-identifier-naming` | `rootR` |
| `:107` | `readability-identifier-naming` | `idS_r` |
| `:109` | `readability-identifier-naming` | `idR_r` |
| `:112` | `readability-identifier-naming` | `idS` |
| `:113` | `readability-identifier-naming` | `idR` |
| `:115` | `readability-identifier-naming` | `dbS` |
| `:116` | `readability-identifier-naming` | `dbR` |
| `:122` | `readability-identifier-naming` | `pS` |
| `:127` | `readability-identifier-naming` | `pR` |

Also: `cppcoreguidelines-pro-type-member-init` fired on
`include/spoink/session/state_machine.h:79` for un-initialised fields
`chunk_hash` and `transfer_id` in an event struct — this is a pre-existing issue
in the header that was not visible to clang-tidy before because no translation
unit that includes the header was added in prior fix rounds (the new test files
are the first to trigger it).

**Required fix:**

1. Rename short-suffix variables in `sec_test.cpp` and `m2_e2e_test.cpp` to full
   snake_case names (e.g. `root_a` → `root_sender`, `id_a_r` → `id_sender_r`,
   `db_a` → `db_server`).
2. Silence `bugprone-unchecked-optional-access` on the dereference that is
   already guarded by `REQUIRE(…has_value())` with a `// NOLINT(bugprone-unchecked-optional-access)`
   comment on that line, consistent with existing NOLINT usage in the project.
3. Add default-member-initialisers to the affected event struct(s) in
   `state_machine.h` to satisfy `cppcoreguidelines-pro-type-member-init`, or add
   a targeted NOLINT if the struct is intentionally left with uninitialised fields.
4. Run `clang-format-18` on all modified files and re-push.

**Acceptance criteria after fix:** all 8 CI jobs pass on PR #5.

---

### Assessment

All five Iteration 1 findings (F1–F5) are correctly resolved.  The security fixes
(F1 session_capability, F2 durable ACK) are verified correct and consistent with
the protocol design.  The state machine single-file enforcement (F4), static_assert
(F5), and test infrastructure additions (F3) are all in place.

The only blocker is the `clang-tidy` CI failure (N-F1) introduced by identifier-
naming violations and one unchecked-optional-access diagnostic in the two new test
files.  These are style violations, not logic errors; the fix is mechanical.

**CHANGES_REQUIRED.**  Fix N-F1 (clang-tidy violations in `sec_test.cpp` and
`m2_e2e_test.cpp`) and confirm all 8 CI jobs pass.  No other changes required.
