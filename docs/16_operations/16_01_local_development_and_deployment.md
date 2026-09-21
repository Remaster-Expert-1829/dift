# Local Development and Deployment

> Status: Approved approach; exact pinned versions/defaults live in repository configuration when implementation begins.

## Supported environment

- Linux x86-64 is the initial CI and benchmark baseline. Other Linux architectures may work but are unsupported until added to CI.
- GCC and Clang builds are required.
- CMake, Ninja, Git, vcpkg, OpenSSL, SQLite, LZ4, Zstd, and test/analysis tools are provided through the pinned developer setup.
- `tc netem`, namespaces, packet capture, and performance tools are optional benchmark dependencies and may require privileges.

## Repository bootstrap target

The initial implementation milestone MUST provide:

```text
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

A documented wrapper MAY offer one-command bootstrap, but it must show what it installs/runs and must not require root for ordinary build/tests. Privileged benchmark setup is separate and explicit.

## Planned repository layout

```text
/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── cmake/
├── config/                 example non-secret configuration
├── docs/
├── include/deltasync/      public/project cross-module headers
├── src/                    modules from architecture document
├── tests/
│   ├── unit/
│   ├── property/
│   ├── integration/
│   ├── e2e/
│   ├── fault/
│   ├── fuzz/
│   └── fixtures/
├── benchmarks/
│   ├── corpus/
│   ├── scenarios/
│   ├── analysis/
│   └── results/            only curated non-sensitive results committed
└── packaging/
```

The architecture owns module boundaries; this physical layout may evolve with documentation updates.

## Runtime directories

The default state/config locations follow the Linux XDG Base Directory convention when available. Exact paths are resolved and printed by `doctor`; overrides use CLI/config documented in `06_02_cli_and_configuration.md`.

- State root and private key directory must not be group/world writable.
- Private key file permissions must be owner-only.
- Chunk/store/staging/destination roots must be local filesystems with required rename/durability behavior; unsupported network filesystem semantics require explicit testing and are not claimed in v1.
- State and store may share a filesystem but staging must share the destination filesystem for atomic finalization.

## First-run procedure

1. Install verified release package or build from the pinned source/dependencies.
2. Run `deltasync init --name <name>` on both devices.
3. Review printed peer IDs and protect state directories.
4. On the receiving device, run `deltasync pair --listen <lan-address:port>`.
5. On the other, run `deltasync pair <lan-address:port>`.
6. Compare SAS on both screens and confirm only if identical and the peer names/IDs are expected.
7. Receiver runs `receive`; sender runs `send`.

V1 does not choose a magic default port in architecture. `TBD-LIMITS-001` selects a documented default before release; until then an explicit port is required.

## Network and firewall

- Bind to an explicit LAN interface rather than all interfaces by default during development.
- Open only the configured TCP listen port while `pair` or `receive` is active.
- Data connections use the same listening endpoint/protocol demultiplexing unless implementation evidence requires a documented port range. A changed port model updates the wire/operations docs.
- No UPnP, NAT traversal, router modification, or internet exposure in v1.
- Public/untrusted Wi-Fi is within the network attacker model, but reachability/firewall risks remain; verify peer identity/SAS.

## Configuration and environment

Use TOML plus non-secret `DELTASYNC_*` overrides. Example configuration contains no real keys/certificates. Key/certificate values are file paths. `doctor` reports configuration precedence, permissions, dependency versions, filesystem capabilities, database schema, and reachability hints without mutating state.

## Capacity planning

Before acceptance, receiver estimates:

- missing plaintext chunk bytes;
- staging bytes for accepted files;
- configured free-space reserve;
- metadata/temporary overhead conservatively.

Deduplication may reduce actual usage but is not relied upon when reserving. Concurrent sessions share a device-level storage budget. If space changes after acceptance, transfer pauses safely.

## Backups

- Use SQLite online backup or stop DeltaSync cleanly before copying metadata.
- Back up metadata, chunk store, identity, and any desired final files as distinct assets.
- Protect identity backup like a credential. Without it, restore uses a new peer ID and requires re-pairing.
- Restored stores run `doctor` and startup reconciliation before receiving or GC.

## Recovery runbook

1. Stop other DeltaSync processes using the state root.
2. Copy state/store for forensic backup if corruption is suspected.
3. Run `doctor` and read the recovery mode/result.
4. Do not delete SQLite/WAL/temp/quarantine files manually while diagnosing.
5. Use documented future repair commands only with dry-run/backup.
6. If reset is required, preserve final files; explicitly reset identity/store and re-pair.

## Containers

Containers are optional for CI/reproducible builds. They are not the primary runtime because host LAN listening, filesystem identity, and destination atomicity must be explicit. A container deployment must mount separate state/store/destination paths with correct permissions and use documented networking; it cannot change the trust model.

## Deployment environments

| Environment | Purpose | Data |
|---|---|---|
| Developer | Local build/unit/integration | Generated fixtures only. |
| Test/CI | Automated isolated peers and fault injection | Ephemeral generated state. |
| Benchmark | Controlled hardware/network/corpus | Reproducible non-sensitive corpus. |
| User/local release | Real direct transfers | Operator-managed files and identities. |

There is no cloud promotion pipeline in v1.
