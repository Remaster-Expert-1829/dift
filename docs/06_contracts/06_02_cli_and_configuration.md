# CLI and Configuration Contract

> Status: Approved command shape; release defaults marked TBD.

## General syntax

```text
deltasync [global-options] <command> [command-options]
```

Global options:

- `--config <path>`: explicit config file.
- `--state-dir <path>`: override state root.
- `--output text|jsonl`: output format.
- `--no-color`: disable color/animation.
- `--log-level error|warn|info|debug|trace`: local verbosity; never enables secret/content logging.

Unknown options and missing arguments are usage errors. Paths follow the local filesystem encoding rules; manifest paths must pass UTF-8/NFC conversion.

## CLI-001 — Initialize

```text
deltasync init --name <device-name>
```

Creates identity and schema if absent. Existing identity returns success with its peer ID unless `--reset-identity` is supplied; reset requires an interactive typed confirmation and is a distinct destructive workflow. Standard output includes device name and peer ID, never the private key.

## CLI-002 — Pair

```text
deltasync pair --listen <host:port>
deltasync pair <host:port>
```

The first form waits for exactly one explicit pairing attempt. The second initiates it. Both display peer name, full peer ID, and the same SAS, then prompt `Codes match? [y/N]`. Non-interactive pairing is not supported in v1.

## CLI-003 — Receive

```text
deltasync receive --listen <host:port> --dest <directory> [--from <peer-id>]
```

The receiver validates state/store/destination roots and prints its local peer ID and listen address. Offers display peer, file count, logical bytes, paths, and overwrite request. Default interactive answer is reject. `--from` narrows the allowed paired peer but does not bypass offer approval.

Unattended allow rules are deferred; `--yes` does not approve network offers in v1.

## CLI-004 — Send and resume

```text
deltasync send <host:port> <input>... [--root <directory>]
               [--overwrite] [--compression off|lz4|zstd|auto]
               [--connections <count>] [--static-scheduler]

deltasync resume <host:port> --transfer <id>
```

Selection rules:

- With `--root`, every input must resolve below that root; manifest paths are root-relative.
- Without `--root`, each file uses its basename and each directory uses its basename as a top-level path. Duplicate results are errors.
- Directory recursion includes regular files and rejects/reports unsupported entries without following symlinks.
- `--overwrite` requests replacement; receiver still approves/effectively decides.
- `--connections` is clamped/rejected against local and negotiated limits and chooses a static requested maximum, not an unlimited promise.

`resume` loads the sender's retained manifest/source mapping for that transfer. If source bytes changed or local sender state is unavailable, it fails with guidance to start a new send.

## CLI-005 — Inspect status

```text
deltasync sessions list [--state <state>]
deltasync sessions inspect <transfer-id>
deltasync report <transfer-id> [--output text|jsonl]
```

Reports distinguish logical, useful, reused, encoded, application-framing, and observed wire bytes; unavailable values are `null` in JSONL and “unavailable” in text.

## CLI-006 — Trust and store maintenance

```text
deltasync peers list
deltasync peers revoke <peer-id>
deltasync store inspect [--verify sample|full]
deltasync store gc --dry-run
deltasync store gc --execute
deltasync doctor
deltasync version
```

Revocation and actual GC require confirmation when interactive. `--execute` expresses GC intent but does not weaken reference/lease checks. `doctor` is read-only unless an explicit future repair subcommand is used.

## Exit codes

| Code | Meaning |
|---:|---|
| 0 | Requested operation completed successfully. |
| 2 | CLI usage error. |
| 3 | Configuration/environment error. |
| 4 | Authentication, pairing, or trust failure. |
| 5 | Offer/operation rejected by peer or destination policy. |
| 6 | Partial batch result: at least one accepted file completed and at least one did not. |
| 7 | Paused/resumable transfer. |
| 8 | Non-resumable transfer/session failure. |
| 9 | Local integrity, store, or recovery failure. |
| 10 | Unexpected internal failure. |

Signal termination follows shell conventions where possible; state safety is still governed by FR-027.

## Output contract

- Prompts and human progress go to the controlling terminal when available.
- Final human summary goes to stdout; actionable local errors go to stderr.
- `jsonl` emits one UTF-8 JSON object per line with `schema`, `timestamp`, `level`, `event`, `transfer_id?`, and event fields.
- Non-TTY text output is line-oriented and contains no cursor movement.
- Sensitive local absolute paths are shown to the local interactive operator but redacted from default persistent logs. JSON reports may include normalized relative paths, not source/destination roots unless explicitly requested.

## Configuration precedence

Highest wins:

1. CLI option.
2. `DELTASYNC_*` environment variable for documented non-secret settings.
3. Selected config file.
4. System/user config file.
5. Documented default.

Invalid higher-priority values are errors; they do not silently fall back. Secret values are never accepted directly through CLI arguments or environment variables. Configuration may specify secret **file paths**.

## Configuration registry

| ID | Key family | Classification | Notes |
|---|---|---|---|
| CFG-PATH-001 | `paths.state`, `paths.store`, `paths.staging` | Operational | Must be canonicalized, permission-checked, and compatible with atomic operations. |
| CFG-NET-001 | `network.listen`, `network.connect_timeout`, `network.operation_timeout` | Operational | Release defaults pending `TBD-LIMITS-001`. |
| CFG-NET-002 | `network.max_connections_per_session/device`, `network.max_in_flight_*` | Safety/configurable | Bounded by compiled absolute ceilings. |
| CFG-TLS-001 | `tls.identity_key_file`, `tls.certificate_file` | Operational | Values are paths; key contents are secrets. TLS 1.3 minimum is not configurable downward. |
| CFG-CDC-001 | `chunking.profile` | Compatibility | Accepted manifests record all parameters. Default pending EXP-CDC-001. |
| CFG-COMP-001 | `compression.mode`, thresholds, codec levels | Configurable/experimental | Defaults pending EXP-COMP-001. |
| CFG-SCHED-001 | Scheduler bounds/windows/coefficients | Configurable/experimental | Profile is recorded in benchmarks, not manifests. |
| CFG-STORE-001 | Store/DB limits, free-space reserve, reconciliation budget | Operational/safety | Cannot exceed compiled ceilings or filesystem capacity. |
| CFG-RET-001 | Session/chunk/log/quarantine retention | Operational | Conservative retain-on-invalid behavior. |
| CFG-OBS-001 | Log/report destination and level | Operational | No secret/content logging at any level. |

## Configuration file

V1 uses one documented UTF-8 TOML configuration schema. Unknown keys are errors by default to catch mistakes. A future `compat.allow_unknown_config` escape hatch requires an ADR/security review; it is not in v1.

The effective non-secret configuration and source of each value can be printed by `doctor`. Secrets are represented as `<redacted>`.
