# Nexora Phase 14 — Integration, Validation, Benchmarking, and Stability

## Objective

Phase 14 is a correctness and integration gate. It adds executable evidence that the
kernel's AI-native primitives can be composed safely before the project expands into
real accelerator execution.

## Implemented components

### 1. Work-graph validation

`ai_work_graph_validate()` now checks:

- null graph nodes
- duplicate node IDs
- missing dependencies
- self-dependencies
- duplicate dependencies
- invalid/null tensor references
- empty device masks
- dependency cycles

Cycle detection is bounded by `AI_MAX_WORK_NODES` and performs no dynamic allocation.

### 2. Scheduler completion and deadlock detection

The scheduler now tracks:

- dispatch count
- pick count
- candidate-node scans
- deadlock count

`ai_scheduler_run_to_completion()` executes the simulated graph until completion or
returns a structured deadlock report when no runnable node exists.

The scheduler remains a simulator: marking a work node done does not execute a real
CPU/GPU/NPU operator.

### 3. Tensor accounting and validation

Tensor creation now rejects:

- null names/shapes
- zero dimensions
- invalid rank/dtype/location
- integer overflow in element or byte calculation

Runtime accounting exposes:

- tensor count
- total tensor bytes
- bytes per tensor location

### 4. Early-heap accounting

The bump allocator now exposes:

- used/capacity
- allocation count
- requested bytes
- alignment padding
- high-water mark
- preflight allocation feasibility

Non-power-of-two alignment is rejected.

### 5. Deterministic scheduler benchmark

`ai_benchmark_run()` executes four chain scenarios with:

- 4 nodes
- 8 nodes
- 16 nodes
- 32 nodes

It records graph nodes/edges, dispatches, picks, and candidate scans. This is a
reproducible algorithmic-overhead benchmark, not a wall-clock benchmark.

### 6. End-to-end Phase 14 stress suite

`ai_phase14_run()` validates:

- tensor accounting
- priority/deadline scheduler behavior
- dependency refresh
- cyclic-graph rejection
- runtime deadlock detection
- capability denial/allow behavior
- 48 repeated transfer → compute → activation DAGs
- benchmark completion
- memory-accounting invariants

On the kernel boot path, Phase 14 is a gate: a failed report triggers `panic()`.

### 7. Host regression harness

`make test-host` compiles the same allocator and AI runtime sources into a normal
host executable and runs 60 assertions. This provides fast validation without GRUB,
QEMU, or an ISO toolchain.

`make verify-phase14` runs the host suite and links the freestanding kernel ELF.

## Acceptance criteria

Phase 14 is considered successful when:

1. host regression suite passes with zero failures;
2. ASan/UBSan execution is clean;
3. kernel sources compile with `-Wall -Wextra -Werror`;
4. the freestanding x86-64 kernel links with no unresolved symbols;
5. all 48 end-to-end stress rounds complete;
6. the deliberate cyclic graph is detected by both validator and scheduler;
7. capability checks deny rights not present in the capability bitset;
8. early-heap accounting remains internally consistent.

## Important scope limitation

The Phase 12/13 project ZIP was not accessible in the current conversation file
surface. This implementation therefore uses the available `aikernel-starter.zip` as
the concrete source base. The Phase 14 modules were deliberately kept additive and
bounded so they can be ported into the current Phase 13 branch with minimal API
conflict.

This package must not be interpreted as proving that unavailable Phase 11–13 code was
integration-tested. It proves the Phase 14 mechanisms against the source tree actually
available for verification.
