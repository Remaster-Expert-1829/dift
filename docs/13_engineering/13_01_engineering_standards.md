# Engineering Standards

> Status: Approved

## Language and build

- Production code uses C++17. Compiler-specific extensions are disabled unless isolated behind a documented platform adapter.
- CMake is the only supported build definition; checked-in IDE project files are not sources of truth.
- `CMakePresets.json` defines developer, CI, sanitizer, and release configurations.
- V1 targets Linux with GCC and Clang. Exact minimum compiler/CMake versions are selected and pinned when the initial toolchain CI is created.
- Project code builds with warnings enabled and warnings-as-errors in CI. Third-party headers are treated as system/external and do not inherit project warning failures.
- Release builds retain safe runtime checks and symbols suitable for separate debug packages; assertions do not replace validation.

## Dependency management

- A vcpkg manifest with a pinned baseline is the preferred reproducible dependency source for OpenSSL, SQLite, LZ4, Zstd, the test framework, and any CBOR helper.
- CMake consumes dependencies through `find_package`; business modules do not depend on package-manager paths.
- System-package builds MAY be supported for distributions but must record actual versions.
- Dependency additions require purpose, license, maintenance/security review, and removal impact.
- Exact versions and hashes are locked in repository files, not prose.
- Runtime never downloads code or dependencies.

If vcpkg cannot supply a required library/license/build mode on the selected Linux CI image, `TBD-ENG-DEP-001` permits switching to another pinned mechanism through a documentation change; it does not justify unpinned `FetchContent`.

### M0 dependency resolution (`TBD-ENG-DEP-001`)

M0 resolves `TBD-ENG-DEP-001` for the current skeleton. The repository uses vcpkg manifest mode with baseline commit `74e6536215718009aae747d86d84b78376bf9e09` (vcpkg release `2025.10.17`) and a `Catch2 3.11.0#0` test-only dependency. The baseline is locked in `vcpkg-configuration.json`; the dependency override is locked in `vcpkg.json`; licenses and port-tree evidence are recorded in `third_party/NOTICE.md`. `cmake/bootstrap-vcpkg.sh` obtains precisely that baseline and rejects a different existing checkout. No future runtime dependency is declared or linked during M0.

The M0 Linux CI baseline is Ubuntu 24.04 with CMake 3.28.3, Ninja 1.11.1, GCC 13.3.0, and LLVM/Clang/clang-tidy/clang-format 18.1.3. CI installs the corresponding packages, uses the selected compiler paths in CMake presets, and runs `scripts/check-toolchain.py`, which parses authoritative version tokens and rejects any value other than those exact reviewed versions. A developer build may happen to work with a newer distribution package; that is not an M0 toolchain-lock claim until it is reviewed and the pinned records are updated. Tool-license notices and the controlled update procedure are in `third_party/NOTICE.md` and the operations guide.

## Formatting, linting, and analysis

- One checked-in `.clang-format` defines formatting; CI verifies no diff after formatting.
- `.clang-tidy` enables correctness, bug-prone, performance, concurrency, and security-relevant checks with documented suppressions.
- GCC/Clang warnings, clang-tidy, and at least AddressSanitizer plus UndefinedBehaviorSanitizer run in CI/test jobs.
- ThreadSanitizer runs on concurrency-focused tests in a compatible separate job.
- Fuzz targets use libFuzzer or an equivalent Clang-compatible engine and preserve regression inputs.
- Suppressions include owner, reason, scope, and removal condition; blanket directory suppressions are forbidden for project code.

## Type and ownership rules

- Prefer RAII for file descriptors, sockets, TLS objects, SQLite handles/statements, locks, temp files, and leases.
- Raw pointers are non-owning unless explicitly documented. Ownership uses values or smart pointers chosen by lifetime semantics.
- Byte buffers are unsigned byte types and explicit views; never use C strings for binary data.
- Strong/domain types wrap peer IDs, transfer IDs, hashes, offsets, lengths, paths, and request IDs to prevent mixing.
- All network/disk length arithmetic is checked before conversion or allocation.
- Immutable data is preferred across worker boundaries. Shared mutable state has one documented synchronization owner.
- Global mutable state is forbidden except process-level facilities encapsulated behind interfaces.

## Naming

- Types and concepts: `PascalCase`.
- Functions/variables/files: `snake_case`.
- Constants: `kPascalCase` or the checked-in formatter/linter-consistent project convention; choose once in the initial skeleton.
- Stable protocol/error/config identifiers use the exact casing documented in contracts.
- Names express units (`timeout_ms`, `bytes`, `offset`) and avoid ambiguous `size` at boundaries.

## Module dependencies

Domain types and pure policy code do not depend on CLI, sockets, SQLite, or concrete filesystems. Adapters depend inward on contracts. Circular library dependencies are forbidden.

```text
cli / transport / sqlite / filesystem adapters
                 ↓
application session orchestration
                 ↓
domain models, validation, pure scheduler/chunk policy
```

OpenSSL, SQLite, and codecs are wrapped at their boundary so tests can inject failures and project errors remain stable.

## Error handling

- Use the `Result<T, Error>` convention from IC-001–012. Do not use sentinel values or `errno` outside adapters.
- Never ignore a returned result. Intentional discard uses an explicit helper and comment.
- Catch exceptions only at third-party or process/task boundaries, translate them, and preserve safe diagnostics.
- Destructors do not throw.
- Error messages do not decide retry behavior; stable error codes/categories do.
- Public/remote errors are safe and bounded. Local nested diagnostics may include OS/library codes but follow redaction rules.

## Concurrency

- Every queue is bounded by items and/or bytes.
- Lock ordering is documented where more than one lock may be held; prefer not holding locks across I/O or callbacks.
- SQLite transaction scope is explicit and contains no network or bulk file operation.
- Callbacks are never invoked while holding an internal mutex unless the contract says so and tests prove no re-entry.
- Cancellation, shutdown, and object lifetime are covered by tests; detached threads are forbidden.
- Use a fake clock and deterministic executor for pure session/scheduler tests.

## Validation and security

- Validate at trust boundaries and convert to validated domain types once.
- Prepared SQL statements only.
- No shell command construction from file/network input. Benchmark scripts quote inputs and require explicit privilege operations.
- Random identifiers/secrets use the OS/TLS-library CSPRNG only.
- Constant-time comparison is used for session capabilities and secret-derived authenticators, not general hashes where secrecy is irrelevant.

## Configuration

- One typed configuration model validates merged sources and emits effective non-secret settings.
- Invalid/unknown settings fail startup rather than falling back.
- Experimental values are named, recorded in reports, and never scattered literals.
- Absolute safety ceilings are compile-time/project constants; operational defaults can be lower.
- Secrets are read from permission-checked files, not source, CLI literals, or environment values.

## Tests and code review

- New behavior includes tests at the lowest useful level plus integration coverage for boundaries.
- Bug fixes first add a failing regression test where reproducible.
- Cryptography, protocol parsing, paths, persistence, retry/idempotency, and state transitions require reviewer attention and cannot be approved solely from happy-path tests.
- Generated files are reproducible and not hand-edited.
- A change is reviewable: one coherent objective, relevant docs/tests, and no unrelated formatting churn.

## Git

- Use meaningful conventional commits where practical: `feat`, `fix`, `docs`, `test`, `refactor`, `perf`, `build`, `ci`, `chore`.
- Commits must not contain keys, certificates, raw private test data, build outputs, or benchmark captures with sensitive paths.
- Rewrite/rebase policy is repository-owner preference; no additional ceremony is an architecture requirement.
