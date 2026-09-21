# Failure and Recovery Policy

> Status: Approved

## Philosophy

Correctness failures fail closed. Transient transport failures may retry only idempotent work within a budget. The receiver's durable state is authoritative for resume. Performance features may degrade to static/raw/sequential behavior; security and data-integrity checks may not.

## Error model

Each error has:

- stable code and category;
- local safe message and correlation ID;
- retry class: `NEVER`, `IMMEDIATE_IDEMPOTENT`, `BACKOFF`, `AFTER_USER_ACTION`, or `NEW_SESSION`;
- scope: connection, chunk attempt, file, session, store, or process;
- resumability flag;
- sensitive internal diagnostic retained locally only.

## Retry rules

- Retry only operations documented as idempotent.
- Use bounded exponential backoff with jitter for transient network errors; exact base, factor, cap, and attempts are `CFG-RETRY-*` selected before release.
- A peer-provided retry delay is clamped to local maximum.
- Authentication, malformed protocol, path violation, changed source, deterministic integrity mismatch, and unsupported version are not automatic retries.
- Retry budgets count across reconnections for one session so reconnect cannot reset an infinite loop.
- Losing an ACK permits resubmission; duplicate verified chunk returns `ALREADY_PRESENT`.

## Failure matrix

| Failure | Retry? | Fallback / rollback / degrade | User result | Log / alert |
|---|---|---|---|---|
| DNS/address/connect timeout | Bounded backoff | Session remains uncreated or pauses if existing | Clear reachability/resume guidance | Warn with endpoint redacted as configured |
| TLS/auth/pin failure | No automatic retry | Close connection; no plaintext protocol | Authentication failure | Security event; repeated attempts counted |
| Control connection loss | Reconnect/resume within budget | Stop new scheduling; data connections drain/close; rotate capability on resume | Paused if exhausted | Warn with transfer ID |
| Data connection loss | Retry unacknowledged chunks | Scheduler reduces/drains pool; other connections continue | Progress or paused | Connection and retry telemetry |
| Application timeout | Bounded backoff | Mark attempt unknown; query/retry idempotently | Progress or paused | Timeout class and latency context |
| Receiver busy/backpressure | After bounded delay | Reduce in-flight work/connections | Continuing/degraded | Decision event |
| Source file changed | No | Stop affected file; do not send stale manifest bytes | Failed/partial; start new send | Error with relative path locally |
| Source read error | After user action or limited retry for transient OS code | Stop affected file | Failed/partial | OS error locally |
| Unsupported file type/path | No | Reject before content | Usage/offer error | Validation event |
| Manifest malformed/digest mismatch | No | Reject provisional session and payloads | Transfer failed | Protocol/security event |
| Chunk decode error | At most bounded clean resend | Sender may resend raw if source hash still matches | Failed if repeated | Integrity event |
| Chunk hash/length mismatch | One bounded reread/re-encode; then no | Do not store/ACK | Failed file/session | High-severity integrity event |
| Duplicate chunk | No special retry | Idempotently verify existing state and ACK | Normal progress | Debug/metric only |
| Disk capacity exhausted | After operator action | Stop accepting payloads; preserve verified state; cleanup temporary files | Paused/resumable where safe | Error and capacity metric |
| Chunk blob write/flush/rename error | Only if OS error classified transient and no ACK | Remove/quarantine temp; do not commit receipt | Paused/failed | Error with local path redacted |
| SQLite busy | Bounded local retry | Backpressure; keep transaction short | Continuing or paused | Busy duration/attempt metric |
| SQLite unavailable/corrupt | No normal retry | Stop ACKs; enter recovery/read-only mode | Store failure | Critical local alert |
| Crash between blob and DB commit | Sender may retry after restart | Reconciliation adopts verified orphan or removes/quarantines | Resumable | Recovery event |
| DB row says verified but blob missing/corrupt | Request again if possible | Quarantine/invalidate receipts; never reconstruct | Paused/failed | Critical integrity alert |
| Reconstruction read/write error | After action; bounded transient retry | Remove/retain staging per policy; final untouched | Failed/partial | Error |
| Whole-file hash mismatch | No blind repeat | Quarantine staging; reverify source/chunks; new session or diagnostic repair | Failed | Critical integrity event |
| Atomic replace failure | After operator action | Preserve existing final and verified staging when safe | Failed/resumable finalization | Error |
| Telemetry sink slow | No correctness retry | Drop low-priority events; increment drop counter | Transfer continues | Reserved summary reports drops |
| Scheduler internal error | No adaptive retry in same epoch | Switch to configured static safe policy | Transfer continues with warning | Error + fallback event |
| Compression encoder failure | Before send, raw fallback if allowed | Send verified plaintext raw | Continue/degraded | Warn/metric |
| Compression decoder failure | See chunk decode | NACK; never store output | Retry/fail | Integrity event |
| Operator cancel | No | Stop new work, safely finish/abort writes, persist state per cancel policy | Cancelled | Audit event |
| Forced process/power loss | Resume after startup | Reconcile; mark sessions paused; final files remain valid | Resumable/diagnostic | Dirty-start recovery report |

## Transaction and rollback boundaries

- Offer rejection creates no accepted content state.
- Accepted manifest insertion is one SQLite transaction; partial graphs roll back.
- Chunk blob installation and DB receipt span filesystem/DB boundaries and rely on ordered commit plus reconciliation, not false atomicity.
- File finalization is per-file. A batch is not transactionally atomic.
- Completed files are not rolled back because another batch file fails.
- Overwrite preserves prior file until verified replacement rename.

## Startup modes

| Mode | Condition | Allowed actions |
|---|---|---|
| Normal | Identity, schema, reconciliation, and mandatory checks pass | All approved commands. |
| Degraded read-only | DB/store inconsistency requiring operator action | Inspect/export diagnostics; no receive, GC, or completion claims. |
| Identity recovery | Missing/incomplete key/cert | Diagnose or explicit reset; no network session. |
| Migration required | Supported older schema | Back up and apply migration before normal mode. |
| Incompatible | Newer/unknown schema or protocol data | Refuse mutation; provide version guidance. |

## Notifications

Interactive users receive plain-language action plus transfer/error code. Remote peers receive only stable safe error codes and retry hints. Critical local store/security failures are emitted to stderr, structured logs, and terminal result. V1 has no external paging service; integration is possible through JSONL/log collection.

## Chaos/fault-injection obligations

Tests inject failure before and after every durable step: temp creation, writes, flush, rename, directory durability, DB begin/update/commit, ACK send, staging write/flush, final rename, and terminal state commit. Each observed state must map to a documented reconciliation result.
