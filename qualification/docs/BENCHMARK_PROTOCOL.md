# Phase 16 Benchmark Protocol

## Experimental design

Use the same host, QEMU version, CPU affinity policy, VM memory, vCPU count, kernel
configuration, and workload seed for baseline and candidate. Record toolchain and
commit identifiers. Warm-up policy must be identical.

Use at least 30 measured **unique** runs per benchmark scenario in both the locked baseline and candidate. For noisy metrics,
increase runs rather than deleting outliers post hoc. Any exclusion rule must be
specified before data collection.

## Mandatory scenarios

| Scenario | Primary metric | Required invariant |
|---|---|---|
| Boot | success rate | 100% |
| Scheduler graph | median/p99 latency | regression gate |
| Tensor allocation/lifetime | peak memory | regression gate |
| Shared tensor handle | bytes copied | 0 payload bytes |
| Capability isolation | violations | 0 |
| Fault recovery | unrecovered faults | 0 |
| Distributed/remote path | completion + fault recovery | no silent loss |
| Telemetry pressure | dropped records | 0 in qualification profile |

## Statistical reporting

Report raw runs, median, p99, maximum peak memory, success rate, and absolute error
counts **per benchmark scenario**. Do not pool latency distributions from heterogeneous tests. Do not present only averages. Tail latency matters because deadline-aware AI
scheduling can appear healthy at the mean while failing under contention.

The supplied validator uses linear-interpolated p99 and relative regression:

`(candidate - baseline) / baseline * 100`

## Baseline locking

A baseline is valid only if it corresponds to a known-good Phase 15 build with the
same benchmark protocol and itself satisfies the blocking baseline-integrity gates. Store the baseline CSV together with commit/build metadata.
Do not replace the baseline simply because a candidate fails a gate.
