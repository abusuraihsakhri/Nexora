# Nexora Phase 13 — Observability, Metrics, Tracing and Diagnostics

## Objective

After Phase 12, Nexora can identify unhealthy domains, bound retries, quarantine failures, enter safe mode, and preserve a deterministic reliability journal. The next requirement is to make normal and abnormal AI execution measurable without introducing dynamic allocation, hidden blocking, unbounded logs, or a new privilege path.

Phase 13 therefore adds a small kernel observability plane built around four primitives:

1. fixed-capacity metrics;
2. bounded structured traces;
3. bounded latency spans;
4. metadata-only diagnostic snapshots.

The subsystem is designed to answer questions such as:

- How many work items were submitted/completed?
- How long did scheduling, transfer, device execution, or recovery take?
- Which device/agent/graph produced a trace event?
- Did telemetry history wrap and lose old records?
- Can a diagnostic consumer prove that two snapshots represent different execution histories?
- Did the Phase 12 journal itself overrun before observability mirrored it?

## Core invariants

1. **Bounded memory.** Metrics, traces, histogram buckets, and active spans use fixed arrays.
2. **No hidden allocation.** The core uses no heap and no libc.
3. **Explicit loss.** Trace-ring overwrites and reliability-source gaps are counted.
4. **Facts, not authority.** Observability cannot authorize, schedule, reset, quarantine, or recover work.
5. **Visibility is data-plane metadata.** Export helpers enforce public/operator/sensitive clearance.
6. **Numeric keys in the kernel.** Human-readable metric names belong to a versioned external schema, not dynamically allocated kernel strings.
7. **Caller-supplied time.** The subsystem is independent of a specific clock implementation.
8. **Deterministic diagnostics.** Metric and trace checksums depend only on stored state and event order.
9. **No payload capture by default.** Tensor/model buffers, secrets, capability tokens, and arbitrary user memory are not copied into traces.
10. **Single-writer/externally synchronized integration.** As in Phase 12, internal locking is deliberately not imposed yet.
11. **Checksums are diagnostic, not cryptographic.** FNV-derived checksums detect divergent state in tests/replay workflows; they are not MACs and do not authenticate telemetry against an attacker.

## Metric model

Each metric has:

- stable numeric `key`;
- kind: counter, gauge, or histogram;
- unit;
- scope kind and scope ID;
- visibility class;
- update count and timestamp.

Histograms use caller-provided, strictly increasing, fixed upper bounds. The final bucket is an overflow bucket. Summary state includes count, saturating sum, minimum, and maximum.

Counters and histogram sums saturate at `u64` maximum rather than wrapping silently.

## Trace model

Each trace event contains:

- sequence number;
- timestamp;
- event kind;
- severity;
- visibility;
- span ID;
- correlation ID;
- actor ID;
- object ID;
- two numeric arguments.

The trace ring is bounded. Once full, the oldest entry is overwritten and `dropped_events` increments. Consumers therefore know that absence from the current ring is not proof that an event never occurred.

## Visibility model

Three kernel visibility labels exist:

```text
PUBLIC < OPERATOR < SENSITIVE
```

Raw trace/metric getters are intended only for trusted kernel code. User-facing or less-privileged diagnostics should use export/read helpers with an explicit clearance level.

Visibility labels do not replace Phase 11 capabilities. A real syscall/debug ABI must first authorize access, then apply the Phase 13 visibility filter.

## Span model

A span represents a bounded in-flight operation such as:

- scheduler decision;
- work execution;
- DMA/transfer;
- device operation;
- network operation;
- recovery;
- checkpoint;
- syscall;
- agent operation.

`ai_obs_span_begin()` allocates one slot from the fixed active-span table and emits a begin trace. `ai_obs_span_end()` computes duration, optionally records it into a histogram, emits an end trace, and frees the slot.

If the supplied end timestamp is earlier than the start timestamp, duration is clamped to zero and `clock_regressions` increments. This avoids unsigned underflow while preserving evidence of a clock/input problem.

## Diagnostic snapshots

A snapshot captures metadata only:

- metric count;
- active span count;
- trace position/count/drop total;
- trace checksum;
- metric checksum;
- rejected update/span counts;
- clock-regression count.

A snapshot does not copy tensor/model payloads. It is therefore suitable for binding a crash/recovery report to an execution state without accidentally becoming a memory-dump facility. Because its checksums fingerprint the complete stored telemetry state, the snapshot trace record is labeled `SENSITIVE`; export requires sensitive-level clearance in addition to the surrounding Phase 11 authorization.

## Phase 12 reliability mirror

Phase 12 remains the authoritative reliability journal. Phase 13 uses a pull adapter with a sequence cursor.

If the Phase 12 ring overwrites records before they are mirrored, the adapter emits `AI_OBS_TRACE_SOURCE_GAP` and increments `source_gaps`. This prevents a diagnostic consumer from mistaking a partial mirrored history for a complete one.

Reliability records retain the source event sequence in `correlation_id`. Ordinary reliability records are operator-visible; capability-violation and internal-invariant fault records are elevated to `SENSITIVE`.

## Concurrency contract

The implementation has no internal locks and is not multi-writer safe.

Integration must use either:

- the owning scheduler/resource lock; or
- a single-writer telemetry path with per-CPU/per-domain staging added in a future phase.

Adding hidden spinlocks here would prematurely dictate the SMP architecture and could make telemetry itself a latency source.

## Performance intent

Phase 13 favors predictable overhead over feature breadth:

- O(1) counter/gauge update after metric lookup cost;
- O(bucket-count) histogram observation, bounded by 16 comparisons;
- O(1) trace append;
- bounded scan for active span lookup;
- bounded scan for export/snapshot checksums.

A future integrated kernel can replace linear metric/span lookup with stable indexed handles or per-CPU structures after measurements justify the added complexity.
