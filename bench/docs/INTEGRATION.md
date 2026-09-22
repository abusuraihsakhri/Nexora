# Phase 14 → Phase 15 Integration

The kernel adapter in `integration/kernel/` deliberately depends on only two platform hooks:

```c
uint64_t nx_bench_now_ns(void);
void nx_bench_write(const char *data, size_t len);
```

Bind them to the Phase 14 monotonic clock and serial/log writer.

## Recommended merge points

### Scheduler

Emit one duration sample around the scheduler selection/dispatch path:

```c
uint64_t t0 = nx_bench_now_ns();
/* existing Phase 14 scheduling decision */
uint64_t t1 = nx_bench_now_ns();
nx_bench_emit_u64("scheduler.dispatch", "latency_ns", t1 - t0, "ns", iteration,
                  "graph", "mixed");
```

### Tensor allocator/reclaimer

Measure allocation and final-consumer reclamation. Emit peak bytes separately as a gauge.

### Shared handles / zero-copy

For both copy and shared-handle variants, emit latency and `bytes_copied`. The expected zero-copy benefit must be demonstrated by the metric, not inferred from the code path name.

### Capability path

Measure allowed and denied checks separately. Avoid logging subject identifiers or secret-bearing resource names.

### Distributed layer

Tag remote/simulated transport and payload size so comparisons are stratified rather than pooled incorrectly.

## Serial capture

Capture QEMU/kernel serial output into a file, then retain only lines beginning with `{` and containing `"schema":"nexora.bench.v1"`. The provided `tools/nxbench.py extract` command performs this filtering safely.

Example:

```bash
python3 tools/nxbench.py extract raw-serial.log --output results/run.jsonl
python3 tools/nxbench.py summarize results/run.jsonl --output results/run.summary.json
```

## Important limitation of this package

The Phase 14 archive was not accessible in the current conversation. Therefore this package does not patch Phase 14 source files by guessed path or guessed function names. The adapter boundary is the only required kernel contract and should be wired to the real tree during merge.
