# Nexora Phase 6 Validation Report

Validation date: 2026-09-20
Release: **0.6.1**

## Acceptance checks

- Normal build: **PASS** — all five C benchmarks compile with `-std=c11 -O2 -Wall -Wextra -Werror`.
- Python syntax/import check: **PASS**.
- Python unit tests: **PASS (6/6)**.
- Executable smoke tests: **PASS** for allocation, graph scheduling, tensor lifetime, zero-copy IPC, Nexora fixture ingestion, suite orchestration and report generation.
- Sanitizer smoke executions: **PASS** with AddressSanitizer + UndefinedBehaviorSanitizer.
- Full benchmark matrix single-run validation: **PASS (14 records)**.
- Repeated quick-suite validation: **PASS (30 records; 3 repetitions × 10 parameterized commands)**.
- Telemetry validation: **PASS** — validation records conform to `nexora.bench.v1` and contain finite numeric metrics.
- Pairwise parameter parity: **PASS** — both Linux variants of every directly compared benchmark family expose identical parameter sets.
- CPU-affinity safety: **PASS** — an impossible requested CPU causes failure rather than silent unpinned execution.
- Fixture safety: **PASS** — synthetic Nexora example telemetry parses for testing but is rejected by the analysis path.
- Analysis outputs: **PASS** — `summary.json`, `summary.csv`, `comparability.json`, and `report.md` generated.

## Benchmark families exercised

1. `allocation_latency`
2. `graph_scheduling_overhead`
3. `tensor_lifetime_memory_peak`
4. `zero_copy_ipc`
5. `deadline_tail_latency`

## Interpretation check

The canonical validation dataset contains Linux/reference measurements only. The generated report correctly states that no Nexora-vs-Linux performance conclusion is justified until real matched Nexora kernel telemetry is supplied.

The methodology additionally blocks architectural performance attribution when execution conditions are not comparable, including native-host Linux versus software-emulated Nexora timing.

## Cross-check corrections incorporated in 0.6.1

- matched allocation iteration/warm-up parameters;
- corrected zero-copy child exit-status handling;
- fail-closed requested CPU affinity;
- explicit cross-platform comparability audit;
- strict numeric telemetry validation;
- synthetic fixture rejection;
- virtualization/timing-parity guidance;
- aligned pre-established shared-handle semantics.

See `CROSS_CHECK_REPORT.md` for the complete audit.

## Integration status

The package remains a Phase-6 overlay intended to sit on top of the cumulative Phase-5 Nexora tree. The actual cumulative Phase-5 source tree is not available in the current file context, so this validation does not claim a source-level ABI merge that could not be checked.
