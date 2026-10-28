# Benchmark Methodology

> Status: Approved methodology. No benchmark result exists until generated from an implementation and archived raw data.

## Questions

| ID | Question |
|---|---|
| BM-Q1 | How many payload and observed network bytes does Spoink transfer for unchanged and modified content? |
| BM-Q2 | What throughput/CPU/memory cost does first transfer impose by workload? |
| BM-Q3 | When does compression reduce total transfer time/bytes, and what does auto routing cost? |
| BM-Q4 | Does adaptive scheduling improve useful throughput or recovery time over Spoink static mode under controlled link changes? |
| BM-Q5 | How much work is avoided during resume after interruption? |
| BM-Q6 | How does Spoink compare with relevant baselines under equivalent scope/configuration? |

## Baselines

- **Spoink static:** same build, TLS, chunking, and compression; fixed one or fixed configured data-connection setting. This isolates scheduler effects.
- **Spoink no-delta/no-compression modes:** isolates feature costs where implemented as benchmark controls.
- **SCP/SFTP:** full encrypted transfer baseline; exact implementation/options/version recorded. It is not a delta-sync baseline.
- **rsync over SSH:** delta-transfer baseline with exact options/version. Claims that rsync cannot tolerate insertions are forbidden; measurement determines comparative behavior.
- Other tools may be added only with documented equivalent semantics and configuration.

Do not compare a loopback Spoink run to a physical-network competitor run, warm cache to cold cache, or encrypted to unencrypted transfer without prominently labeling the mismatch.

## Environments

1. **Pipeline microbenchmark:** one machine, isolated files; not reported as network throughput.
2. **Reproducible network namespace:** Linux namespaces/veth with recorded `tc netem` and rate controls; useful for CI and fault sweeps.
3. **Two-device LAN:** documented hardware/link for user-facing results.

Record OS/kernel, CPU, RAM, storage/filesystem, NIC/link, power mode, compiler/build flags, dependency/tool versions, MTU, TLS, Spoink profiles, and topology.

## Workload corpus

- low-entropy text/log/source corpus;
- already-compressed media/archive corpus;
- deterministic pseudorandom incompressible corpus;
- large binary/VM-like image where licensing permits;
- many-small-file corpus;
- mixed batch with varied sizes.

Change patterns:

- unchanged;
- append;
- insertion near beginning/middle;
- deletion;
- seeded random region replacement at declared logical percentages;
- whole-file replacement.

Corpus generator source, seed, sizes, and hashes are archived. Estimated “1 GB with 5% changes” is not a result until the exact mutation definition is recorded.

## Network matrix

Sweep controlled combinations of:

- baseline latency;
- bandwidth cap;
- jitter;
- queueing policy;
- induced packet loss only as an external TCP stress condition;
- mid-transfer disconnect duration;
- stable versus step-changing conditions.

Exact matrix values are experiment configuration, chosen before a run and stored with results. The report says “link shaped with X” rather than claiming the application directly observed packet loss.

## Run protocol

1. Pre-register tool/config/workload/network matrix and primary metrics.
2. Verify source/destination hashes and free space.
3. Establish cache policy: cold, warm, or both; document cache-clearing method/privileges.
4. Run an unreported warm-up when the measured system has one-time initialization effects.
5. Randomize tool/config execution order within a block to reduce drift.
6. Collect independent repetitions until the preselected uncertainty criterion is met or the configured run budget is exhausted.
7. Default analysis targets a 95% confidence interval for the primary metric with relative half-width at most 5%; if not reached, report the wider interval and run count rather than discarding data.
8. Validate every destination hash after every measured run.
9. Preserve raw reports, command lines, stderr, environment, and failures.

For non-normal/skewed results, bootstrap confidence intervals and report median plus relevant percentiles. Means may be included for comparability but never alone.

## Metrics

- elapsed wall time and time-to-first/last completed file;
- useful plaintext throughput and encoded payload throughput;
- logical/useful/reused/encoded/application-framing bytes;
- observed interface bytes/packets when capture/counters are valid;
- CPU time/utilization, peak RSS, disk read/write bytes, temporary peak;
- connection count over time, RTT/ACK-latency distributions, timeouts/retries;
- compression ratio and encode/decode CPU by decision category;
- resume bytes/time avoided versus restart;
- correctness and failure count.

“Bandwidth saved” is:

```text
1 - (Spoink observed application payload bytes / declared full-file logical bytes)
```

It is labeled application-payload savings unless measured wire bytes are used. Manifest/TLS/TCP overhead is never silently counted as zero.

## Adaptive scheduler evaluation

Compare adaptive and static modes on the same build/corpus/topology, paired by randomized run blocks. Report:

- useful throughput and completion time;
- connections/in-flight bytes timeline;
- CPU/memory/socket cost;
- time to react and recover after condition change;
- failures/retries;
- per-file fairness/starvation.

An improvement claim requires confidence interval/effect size and no correctness/resource regression. Negative/no improvement is a valid result and informs tuning or scope.

## Compression evaluation

Compare `off`, `lz4`, `zstd`, and `auto`. Measure total wall time, bytes, and CPU—not ratio alone. `EXP-COMP-001` selects initial auto thresholds/profile using a training corpus; final reporting uses a separate validation corpus to avoid tuning to the test set.

## CDC evaluation

`EXP-CDC-001` sweeps candidate Rabin window/min/target/max/profile parameters on a declared corpus. Measure chunk distribution, metadata count, re-transfer reuse for change patterns, memory/CPU, and maximum-frame compatibility. The chosen `rabin-v1` profile is then frozen with golden vectors and rationale.

## Reporting rules

- Separate targets, observations, and explanations.
- Never copy roadmap estimates into result tables.
- Include failed/outlier runs with reason; exclusion rules are predeclared.
- Publish raw CSV/JSONL and analysis script with the summarized table/plot.
- Resume/résumé claims use the reproducible measured result and identify workload/environment.
