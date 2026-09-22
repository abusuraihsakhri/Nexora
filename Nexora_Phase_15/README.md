# Nexora Phase 15 — Performance Validation & Regression Gates

Phase 15 provides benchmark and regression-gate tooling intended to make the Nexora research system measurable. It adds a host-side benchmark pipeline, a stable JSONL event contract, regression thresholds, descriptive statistical summaries, and a narrow kernel adapter for emitting benchmark events.

> Integration note: this package is intentionally additive. The current chat did not contain the Phase 14 source archive, so the kernel-facing adapter does not assume undocumented Phase 14 APIs. Bind `nx_bench_now_ns()` and `nx_bench_write()` to the Phase 14 monotonic clock and serial/log sink when merging.

## Goals

- benchmark scheduler dispatch and completion latency
- measure tensor allocation/lifetime and peak-memory behavior
- measure copy vs shared/zero-copy paths
- measure capability-check overhead
- measure local vs distributed/remote-resource path overhead
- produce median, p95, p99, min, max, mean and sample count
- compare candidate results with a locked baseline
- fail on threshold-defined regressions using explicit, version-controlled limits
- keep raw events for reproducibility

## Quick start

```bash
python3 tools/nxbench.py summarize fixtures/sample_run.jsonl --output results/sample_summary.json
python3 tools/nxbench.py compare fixtures/baseline_summary.json results/sample_summary.json \
  --thresholds config/thresholds.json --output results/comparison.json
python3 -m unittest discover -s tests -v
```

Or run the complete self-test:

```bash
./scripts/selftest.sh
```

## Event contract

Each line is one JSON object. Required fields:

```json
{"schema":"nexora.bench.v1","run_id":"run-001","benchmark":"scheduler.dispatch","metric":"latency_ns","value":830,"unit":"ns","iteration":1}
```

Optional fields include `variant`, `workload`, `tags`, `timestamp_ns`, `phase`, `device`, and `metadata`.

## Phase 15 exit criteria

1. Every benchmark run produces parseable `nexora.bench.v1` JSONL.
2. All required benchmark families have at least one metric.
3. The analyzer produces deterministic summaries.
4. Regression thresholds are version-controlled.
5. Candidate-vs-baseline comparison exits non-zero when a required gate fails.
6. Unit tests pass.
7. Kernel integration uses the adapter boundary rather than host-specific dependencies.

See `docs/PHASE15_SPEC.md` and `docs/INTEGRATION.md`.
