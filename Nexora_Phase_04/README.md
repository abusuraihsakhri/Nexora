# Nexora / AIKernel — Phase 4 Step 10 (Phase 4 Complete)

This tree is the cumulative Nexora kernel prototype through **Phase 4 Step 10:
zero-copy benchmark and full Phase 4 integration gate**.

## Phase 4 status

- Step 1 — shared tensor backing architecture: complete
- Step 2 — work domains: complete
- Step 3 — generation-protected per-domain handles: complete
- Step 4 — capability-style handle rights: complete
- Step 5 — cross-domain SHARE / TRANSFER: complete
- Step 6 — shared physical tensor backing + zero-copy mapping: complete
- Step 7 — handle/tensor/backing/mapping lifetime graph: complete
- Step 8 — revocation + SMP/concurrency hardening: complete
- Step 9 — adversarial/fuzz/race/stress validation: complete
- **Step 10 — benchmark + end-to-end integration gate: complete**

## What Step 10 adds

1. `tests/phase4_integration_host_test.c`
   - domain creation/lifecycle
   - rights-attenuated SHARE
   - same-backing cross-domain mappings
   - read-only enforcement
   - transitive SHARE only when delegated
   - TRANSFER source invalidation
   - local revocation semantics
   - pinned mapping-view lifetime
   - final-reference tensor/backing retirement
   - work-graph/scheduler sequencing over the same shared tensor

2. `benchmarks/zero_copy_benchmark.c`
   - payload `memcpy` handoff baseline
   - handle-only SHARE control-plane latency
   - SHARE + MAP + UNMAP + CLOSE control-plane latency
   - zero payload bytes copied by the shared path
   - two-copy-buffer versus one-shared-backing payload footprint model

3. Benchmark artifacts
   - `BENCHMARK_RUNS.csv` — five raw benchmark runs
   - `BENCHMARK_RESULTS.csv` — median summary

4. Final Phase 4 validation and documentation
   - `docs/PHASE4_STEP10.md`
   - `docs/PHASE4_COMPLETE.md`
   - updated `docs/ARCHITECTURE.md`
   - updated `docs/ROADMAP.md`
   - updated `VALIDATION.txt`

## Quick validation

```bash
make build/kernel.elf
make test-host
make test-host-sanitize
make test-host-tsan
make test-host-step9-sanitize
make test-host-step9-tsan
make test-host-step9-stress
make test-host-step10-sanitize
make benchmark-phase4
```

Or run the non-TSan integration gate:

```bash
make phase4-gate
```

## Benchmark caveat

The current mapping layer is a **kernel logical mapping model**, not hardware
per-domain page-table installation. Therefore the nanosecond SHARE/MAP results
are a control-plane microbenchmark, not a claim of real process-to-process IPC
latency or an end-to-end application speedup. The robust Phase 4 result is the
architectural invariant: the shared path transfers authority and mapping
metadata while copying **zero tensor payload bytes**.

See `docs/PHASE4_STEP10.md` for methodology and interpretation.

## Next phase

**Phase 5 — user mode and syscall ABI.** The next major gate is to bind these
objects to real address spaces and a narrow user/kernel ABI, then measure the
same shared-tensor path with actual page tables, syscalls, context switches and
TLB behavior.
