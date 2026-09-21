# Nexora Phase 13

**Kernel Observability, Metrics, Tracing and Diagnostic Snapshots**

Phase 13 extends the validated Phase 12 reliability baseline with a bounded, freestanding observability subsystem for the Nexora AI-native kernel architecture.

## Included

- fixed-capacity metric registry
- counters, gauges, and bounded histograms
- system/scheduler/device/agent/remote/graph/tensor/service scopes
- public/operator/sensitive visibility classes
- bounded structured trace ring with explicit overwrite accounting
- correlation IDs and bounded active spans
- latency recording into histograms
- trace and metric checksums for deterministic diagnostics
- metadata-only diagnostic snapshots
- Phase 12 reliability-journal mirroring with explicit source-gap detection
- host tests, edge tests, sanitizer gate, freestanding compile gate, and unresolved-symbol gate
- cross-phase contracts preserving Phase 11 authorization and Phase 12 recovery ownership

## Build and test

```bash
make test
```

Expected test output includes:

```text
Phase 12 reliability tests: PASS
Phase 12 policy edge tests: PASS
Phase 13 observability tests: PASS
Phase 13 edge tests: PASS
```

For the stronger validation suite:

```bash
make validate
```

This adds AddressSanitizer/UBSan tests and verifies the combined freestanding Phase 12 + Phase 13 core has no unresolved runtime symbols.

## Main files

```text
include/ai/reliability.h        Phase 12 reliability contract
include/ai/observability.h      Phase 13 observability contract
include/ai/obs_reliability.h    Phase 12 -> Phase 13 pull adapter
src/ai/reliability.c
src/ai/observability.c
src/ai/obs_reliability.c
```

## Memory budget

The reference x86-64 build gives `sizeof(ai_observability) == 67,664` bytes. Treat the object as static/global kernel state, not a small-stack local. See `docs/PHASE13_MEMORY_BUDGET.md`.

## Integration order

The security/reliability ordering remains unchanged:

```text
authorize
  -> reliability-admit
  -> schedule / execute
  -> observe facts
```

Observability is not an authorization or recovery authority. It must never make denied work admissible, mutate capabilities, reset devices, or bypass Phase 12 recovery policy.

## Deliberate boundary

Phase 13 records bounded execution telemetry. It does **not** implement a filesystem logger, network telemetry exporter, distributed trace collector, eBPF-style programmable probes, tensor/model payload capture, or autonomous policy decisions. Those require separate threat models and resource budgets.
