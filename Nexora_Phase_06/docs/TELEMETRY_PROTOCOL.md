# Nexora Benchmark Telemetry Protocol v1

Each benchmark result is one JSON object. Linux programs write JSONL directly. Nexora writes the same object after the literal serial prefix `NEXORA_BENCH `.

Required fields:

```json
{
  "schema": "nexora.bench.v1",
  "benchmark": "allocation_latency",
  "platform": "nexora",
  "variant": "ai_tensor_create_release",
  "params": {"iterations": 20000, "size_bytes": 4096},
  "metrics": {"median_ns": 0, "p95_ns": 0, "p99_ns": 0}
}
```

Canonical benchmark IDs are:

- `allocation_latency`
- `graph_scheduling_overhead`
- `tensor_lifetime_memory_peak`
- `zero_copy_ipc`
- `deadline_tail_latency`

For cross-platform comparison, use identical parameter keys and values wherever semantics allow. Platform-specific implementation names belong in `variant`, not in the benchmark ID.

## Recommended Nexora variants

`allocation_latency`: `ai_tensor_create_release`; `graph_scheduling_overhead`: `nexora_graph`; `tensor_lifetime_memory_peak`: `final_consumer_reclaim`; `zero_copy_ipc`: `shared_tensor_handle`; `deadline_tail_latency`: `nexora_deadline_scheduler`.

## Measurement boundaries

Instrument the narrow primitive itself. Do not include serial printing in the timed interval. Accumulate samples in memory, compute summary statistics after the measured loop, then print one record.

## Units

Suffix metric names with the unit (`_ns`, `_bytes`) when practical. Rates are dimensionless fractions in `[0,1]`. Do not silently mix cycles and nanoseconds; if Nexora lacks a calibrated nanosecond clock, emit `_cycles` and keep it out of direct nanosecond comparisons.

## Matching rule

Cross-platform comparison requires the same `benchmark` value and an identical canonical `params` object. Include measurement controls such as `iterations` and `warmup` when the Linux record includes them. If timing sources differ (cycles vs nanoseconds), calibration metadata must be retained outside the individual record and the values must not be treated as directly comparable until converted on a documented basis.

## Test fixtures

Bundled example telemetry may carry `"fixture": true` so parser behavior can be smoke-tested. `analyze.py` rejects fixture telemetry; it must never be mixed into a performance report.
