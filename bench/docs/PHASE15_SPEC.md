# Phase 15 Specification

## Scope

Phase 14 established integration correctness. Phase 15 answers the next research question: **does the integrated architecture exhibit measurable, repeatable performance characteristics without regressions across releases?**

The phase is deliberately split into two layers:

- **kernel instrumentation**: minimal event emission with no statistics inside the kernel;
- **host analysis**: parsing, aggregation, quantiles, comparison, gating and artifact retention.

Keeping statistics out of kernel space reduces code surface and avoids contaminating measurements with analysis work.

## Required benchmark families

| Family | Core metric | Typical variants | Research question |
|---|---|---|---|
| `scheduler.dispatch` | `latency_ns` | fifo, priority, graph, locality | What does scheduling policy cost? |
| `scheduler.tail` | `latency_ns` | deadline, mixed-priority | What happens at p95/p99? |
| `tensor.alloc` | `latency_ns` | sizes/lifetime classes | What is allocation overhead? |
| `tensor.peak` | `bytes` | lifetime-aware vs blind | Does lifetime reclamation reduce peak memory? |
| `tensor.reclaim` | `latency_ns` | final-consumer path | What does eager reclaim cost? |
| `ipc.copy` | `bytes_copied` / `latency_ns` | copy vs shared-handle | Is zero-copy actually reducing copies? |
| `capability.check` | `latency_ns` | allow/deny | What is security-check overhead? |
| `distributed.transfer` | `latency_ns` / `bytes` | local vs remote/simulated | What overhead is introduced by distributed resources? |

## Measurement rules

1. Separate warm-up from measured iterations.
2. Record every measured sample, not only aggregates.
3. Use the kernel monotonic clock used by Phase 14 integration tests.
4. Keep workload parameters in `metadata` or `tags`.
5. Do not compare runs from materially different hardware or emulator settings as if they were interchangeable.
6. Lock a baseline only after a clean run and retain its raw JSONL plus summary.
7. Treat p95/p99 as unstable with very small `n`; default gate requires at least 30 samples per latency metric.

## Gate semantics

Thresholds are relative unless explicitly marked absolute.

For a latency metric where lower is better:

```text
regression_pct = (candidate - baseline) / baseline * 100
```

A gate fails when `regression_pct > max_regression_pct`.

For a metric where higher is better, the direction is reversed. The schema supports `direction = lower|higher`.

A missing required metric is a failure. A metric below `min_samples` is a failure unless the threshold rule sets `allow_insufficient_samples=true`.

## Statistical outputs

For each unique `(benchmark, metric, unit, variant, workload)` key:

- `count`
- `min`
- `max`
- `mean`
- `median`
- `p95`
- `p99`

Quantiles use linear interpolation with the index `(n - 1) * q`, making results deterministic across supported Python versions.

## Non-goals

Phase 15 does not claim an advantage over Linux merely because a benchmark runs. Comparative conclusions require matched workloads, matched hardware, controlled configuration, and confidence intervals across independent runs. This package provides the measurement substrate; it does not manufacture a favorable result.
