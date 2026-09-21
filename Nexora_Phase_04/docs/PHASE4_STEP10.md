# Phase 4 Step 10 — Zero-Copy Benchmark and Integration Gate

## Objective

Close Phase 4 with two independent checks:

1. **Correctness integration:** prove that the domain/handle/rights/delegation/
   mapping/lifetime/revocation path works as one coherent subsystem and remains
   compatible with the existing work graph/scheduler.
2. **Performance characterization:** compare the O(n) payload copy required by
   a conventional handoff with the O(1)-with-respect-to-payload-size metadata
   operations used by Nexora's shared-backing path.

This is a prototype/kernel-architecture benchmark, not a Linux IPC benchmark.

## End-to-end integration test

`tests/phase4_integration_host_test.c` exercises this sequence:

```text
producer domain
   |
   | install READ|WRITE|MAP|SHARE|TRANSFER handle
   v
shared tensor + backing
   |
   +---- SHARE READ|MAP|SHARE ----> consumer
   |                                  |
   |                                  +---- SHARE READ|MAP ----> receiver
   |
   +---- producer RW mapping
   +---- consumer RO mapping
   +---- receiver RO mapping

all mappings -> same physical backing token
all domains  -> distinct logical virtual ranges
```

The test then verifies:

- write visibility across the shared backing;
- consumer write-map rejection;
- scheduler ordering of producer and consumer work nodes over the same tensor;
- successful authority transfer to another domain;
- source token invalidation after transfer;
- local revocation blocking new acquisitions while an existing mapping remains valid;
- local revocation not recursively revoking already delegated descendant handles;
- a pinned mapping view remaining valid after its mapping ID is unmapped;
- mapping-owned lifetime after all handles are closed;
- exact final-reference retirement of tensor metadata and backing;
- complete registry/domain drain at teardown.

## Benchmark methodology

`benchmarks/zero_copy_benchmark.c` tests payload sizes:

- 1 MiB
- 4 MiB
- 16 MiB
- 32 MiB

For each size it measures three paths:

### 1. Copy handoff

```text
source tensor backing --memcpy(N bytes)--> second payload buffer
```

This is the data-movement baseline. The number of copy iterations is chosen so
that each size moves approximately 256 MiB total where practical.

### 2. Handle SHARE

```text
source domain --SHARE--> target-local handle --CLOSE-->
```

20,000 iterations per payload size. No tensor payload bytes are touched.

### 3. SHARE + MAP

```text
SHARE -> read mapping -> verify same physical backing -> UNMAP -> CLOSE
```

64 iterations per payload size. No tensor payload bytes are copied.

Five fresh process runs were collected and the median for each metric is stored
in `BENCHMARK_RESULTS.csv`.

## Measured host results

Environment for this run:

- Linux 6.18.44 x86-64 under KVM
- AMD EPYC 9V74 virtualized CPU
- GCC 14.2.0
- GNU ld 2.44

| Payload | Median memcpy handoff | Median memcpy bandwidth | Median SHARE | Median SHARE+MAP | Payload bytes copied by shared path | Payload footprint: copy vs shared |
|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB | 24.4 us | 40.0 GiB/s | 31.6 ns | 81.1 ns | 0 | 2 MiB vs 1 MiB |
| 4 MiB | 99.1 us | 39.4 GiB/s | 32.7 ns | 81.8 ns | 0 | 8 MiB vs 4 MiB |
| 16 MiB | 0.854 ms | 18.3 GiB/s | 31.0 ns | 91.1 ns | 0 | 32 MiB vs 16 MiB |
| 32 MiB | 2.092 ms | 14.9 GiB/s | 32.8 ns | 87.0 ns | 0 | 64 MiB vs 32 MiB |

The raw five-run data is in `BENCHMARK_RUNS.csv`.

## Interpretation

The result to preserve is **not** the numerical speedup ratio. The current host
model performs handle-table and logical-mapping metadata operations without:

- installing real page-table entries;
- a syscall transition;
- a process context switch;
- TLB invalidation/shootdown;
- scheduler wakeup latency;
- DMA/IOMMU programming;
- NUMA placement;
- device synchronization.

Consequently, a ratio such as `memcpy_ns / share_plus_map_ns` is useful only to
show the different scaling classes in this prototype: payload copy grows with
N, while handle/mapping metadata is approximately payload-size independent. It
must **not** be reported as a real-world end-to-end Nexora speedup.

The robust architectural results are:

```text
copy handoff:
    payload bytes copied = N
    simultaneous payload storage ~= 2N

shared-backing handoff:
    payload bytes copied = 0
    simultaneous payload storage ~= N + metadata
```

For a two-domain handoff, this removes one full tensor-sized data copy and one
full tensor-sized duplicate payload allocation.

## Cache/TLB status

Direct hardware cache/TLB counters were **not measured** in Step 10. Doing so
before real per-domain page tables would be misleading. Phase 5/6 should add:

- hardware page-table mappings;
- syscall/user-mode handoff;
- `perf`/PMU counters where available;
- dTLB load/miss and page-walk measurements;
- cache references/misses;
- context-switch and scheduler latency;
- NUMA-local versus remote placement.

## Acceptance criteria

Step 10 is complete when:

- kernel ELF compiles with `-Wall -Wextra -Werror`;
- all legacy Phase 4 host tests pass;
- Step 9 adversarial/race/stress tests remain green;
- ASan/UBSan remain green;
- ThreadSanitizer remains green on concurrency/race tests;
- the new end-to-end integration test passes;
- the shared benchmark reports identical physical backing and zero payload-copy bytes;
- benchmark limitations are explicitly documented.

All criteria above were met in the recorded validation environment, except
QEMU/GRUB boot/ISO execution, whose tooling is unavailable in this container.
