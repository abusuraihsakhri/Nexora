# Step 10 — Integration, Hardening, Test Harness, and Release Baseline

## Objective

Prove that Steps 1–9 form a coherent operating-system foundation.

Step 10 should add almost no major feature.

## Core invariants

### Memory

- no physical frame allocated twice;
- reserved memory never enters the free pool;
- user pages cannot expose stale kernel/process data;
- executable pages are not writable;
- writable pages are not executable;
- page-table ownership is known;
- process resources are reclaimable.

### Scheduling

- one running thread per active CPU;
- running thread is not queued;
- blocked/sleeping/exited threads cannot be scheduled incorrectly;
- idle thread always exists.

### Isolation

- process A cannot access process B private memory;
- user faults do not panic the kernel;
- invalid syscall input cannot corrupt kernel state.

## Self-test framework

Boot modes:

```text
normal
self-test
stress-test
fault-test
```

Machine-readable output:

```text
[TEST] pmm.basic ........ PASS
[TEST] vmm.map_unmap .... PASS
...
[RESULT] PASS 53/53
```

## Stress suites

### PMM
- exhaustion;
- randomized free order;
- contiguous allocation;
- reserved-range exclusion;
- repeated cycles.

### VMM
- 4-KiB/2-MiB/1-GiB boundary traversal;
- many mappings;
- page-table leak detection;
- process lifecycle repetition.

### Heap
- random sizes/alignments;
- deterministic per-allocation patterns;
- fragmentation metrics;
- millions of operations.

### Scheduler
- CPU-bound;
- yield-heavy;
- sleepers;
- blockers;
- short-lived threads;
- lost-wakeup race testing;
- watchdog for no-progress hangs.

### Processes/syscalls
- null pointer;
- kernel address;
- NX execution;
- read-only write;
- CLI/HLT/UD2;
- invalid syscall number;
- noncanonical/boundary pointers;
- oversized lengths;
- malformed ELF.

## Hardening

Where supported and after the relevant code is prepared:

- `CR0.WP`;
- NX;
- SMEP;
- SMAP.

## Diagnostics

A kernel panic should report:

- reason;
- CPU;
- thread/process;
- RIP/RSP/RFLAGS;
- page-fault details when applicable;
- memory statistics;
- scheduler state;
- recent scheduler events;
- stack trace;
- symbols in debug builds.

## Tooling

Provide reproducible commands conceptually equivalent to:

```text
cargo xtask build
cargo xtask run
cargo xtask test
cargo xtask stress
```

CI should compile the kernel/userspace and boot automated QEMU tests.

## Phase-1 release gate

Phase 1 is complete only when the full acceptance suite repeatedly passes from a clean checkout.

Suggested tag:

```text
Nexora Kernel v0.1.0
Phase 1 Foundation
```
