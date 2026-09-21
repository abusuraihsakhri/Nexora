# AIKernel Research Roadmap

## Milestone 0 — Bootable architecture skeleton

Status: **implemented in this starter**

Goals:

- x86-64 boot
- serial/VGA diagnostics
- basic allocator
- tensor metadata
- work graph
- simple scheduler
- capability prototype

## Milestone 1 — Proper kernel substrate

Implement:

- IDT and exception handlers
- physical memory map parsing
- page-frame allocator
- virtual-memory manager
- kernel heap
- timer/APIC
- monotonic clock
- SMP discovery

Exit criteria:

- reliable boot
- allocations backed by real physical-page management
- scheduler has a time source

## Milestone 2 — AI object model

Current Phase 3 progress:

- [x] common kernel object identity/handle foundation
- [x] tensor classes, shapes, byte strides, overflow-safe sizing, layout metadata
- [x] page-granular tensor physical backing through `NX_OBJECT_MEMORY` (bootstrap early-heap backend)
- [x] explicit object ownership / owner transfer
- [x] strong reference counting, pinning, deterministic destruction
- [x] tensor lifetime classes and policy-aware backing reclamation
- [x] producer/consumer graph with one-producer/multi-consumer provenance
- [x] scheduler readiness derived from tensor producer completion
- [x] final-consumer accounting connected to the automatic reclamation manager
- [x] AI-native work descriptors with work class and QoS metadata
- [x] estimated compute cost, memory traffic, scratch use, and execution duration
- [x] logical tensor-I/O byte accounting
- [x] batch identity and batch size metadata
- [x] allowed device-class mask validation
- [x] locality/placement constraints and concrete preferred/resident device identity
- [x] synthetic CPU/GPU/NPU device registry with direct-link topology
- [x] transfer-cost estimation and first concrete work-to-device placement decision
- [x] automatic final-consumer reclaim manager with deferred retry
- [x] target-based memory-pressure reclaim for graph-dead temporary tensors
- [x] safe cached-tensor eviction with resident-byte accounting
- [x] bounded tensor/work/reclamation trace instrumentation
- [x] graph/tensor/memory diagnostic dumps
- [x] reproducible lifetime-blind vs graph-aware peak-residency experiment

Exit criteria: **met for the Phase 3 logical-residency prototype**

- [x] graph can reclaim a tensor immediately after its final consumer
- [x] memory usage can be measured against a lifetime-blind baseline
- [x] controlled Step 10 demo shows 608 KiB vs 448 KiB peak live residency (26.31% reduction)

Caveat: the bootstrap bump allocator still cannot reuse reclaimed physical pages.

## Milestone 3 — AI scheduler simulator inside kernel

Implement cost function:

```text
cost =
    transfer_cost
  + queue_delay
  + execution_estimate
  + memory_pressure_penalty
  + deadline_penalty
```

Initially use simulated CPU/GPU/NPU devices.

Benchmark:

- FIFO
- priority-only
- graph-aware
- locality-aware

## Milestone 4 — Shared tensor handles

Implement protected handles allowing two isolated work domains to access the same physical tensor backing without copying.

Measure:

- bytes copied
- latency
- cache/TLB behavior
- security isolation

## Milestone 5 — User mode and syscall ABI

Minimal ABI:

```text
ai_tensor_create
ai_tensor_map
ai_tensor_release
ai_work_submit
ai_work_wait
ai_cap_delegate
ai_device_query
```

Avoid reproducing POSIX unless compatibility becomes necessary.

## Milestone 6 — Host-side comparison harness

Build equivalent Linux benchmark programs.

Compare:

- allocation latency
- graph scheduling overhead
- tensor lifetime memory peak
- zero-copy IPC
- deadline tail latency

## Milestone 7 — Real device path

Do **not** start with a modern NVIDIA GPU driver.

Better progression:

1. virtio-style simulated accelerator
2. PCI enumeration
3. simple DMA-capable experimental device
4. accelerator backend through an intentionally narrow interface
5. only then consider real GPU hardware

## Milestone 8 — Inference-oriented experiments

Research targets:

- persistent model objects
- KV-cache residency
- dynamic batching
- request deadlines
- tensor prefetch
- memory-pressure-aware admission control

## Milestone 9 — Distributed AI resources

Represent:

- remote GPU
- remote tensor
- network transfer
- collective operation

as graph/resource objects.

Potential experiments:

- topology-aware placement
- RDMA-style transfer abstraction
- collective scheduling

## Milestone 10 — Agent capability model

Create per-agent capability domains.

Examples:

```text
agent A:
    tensor X: read
    model M: execute
    GPU 0: use <= 20%
    network: disabled

agent B:
    dataset D: read
    model M: execute
    network: destination whitelist
```

The security model should be measurable and formally specified before being trusted.
