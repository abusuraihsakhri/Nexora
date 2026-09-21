# Porting Phase 14 onto the current Nexora Phase 13 branch

The source package in this archive was built from the `aikernel-starter.zip` that was
accessible to this conversation. If your Phase 13 branch contains newer implementations
of these modules, port the Phase 14 changes by behavior rather than overwriting newer
subsystems wholesale.

## New files that are normally safe to add

- `include/ai/benchmark.h`
- `include/ai/integration.h`
- `src/ai/benchmark.c`
- `src/ai/integration.c`
- `tests/host_panic.c`
- `tests/test_phase14.c`
- `docs/PHASE14.md`
- `docs/PHASE14_TEST_REPORT.md`

## Existing modules touched

- `include/kernel/types.h`
- `include/kernel/memory.h`
- `src/mm/bump.c`
- `include/ai/tensor.h`
- `src/ai/tensor.c`
- `include/ai/work.h`
- `src/ai/work.c`
- `include/ai/scheduler.h`
- `src/ai/scheduler.c`
- `src/kernel/kmain.c`
- `Makefile`
- `docs/ROADMAP.md`

If Phase 13 already has a real heap, timer, distributed graph model, or richer
scheduler, adapt the metrics and validators to those APIs rather than replacing them
with the starter implementation.

## Required semantic behavior after porting

1. Graph validation rejects structural corruption and cycles.
2. Scheduler exposes completion/deadlock rather than silently stalling.
3. Tensor-byte and memory-allocation accounting are queryable.
4. Capability allow/deny checks are covered by integration tests.
5. A deterministic benchmark produces machine-comparable counters.
6. A repeated end-to-end stress workload is part of the test gate.
7. The kernel build remains warning-clean.
