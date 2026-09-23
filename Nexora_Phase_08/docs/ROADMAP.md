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

Extend tensor objects with:

- physical backing
- ownership
- reference counting
- lifetime class
- producer/consumer graph
- locality information

Extend work nodes with:

- estimated compute cost
- estimated bytes moved
- device constraints
- batch identity

Exit criteria:

- graph can reclaim a tensor immediately after its final consumer
- memory usage can be measured against a conventional lifetime-blind allocator

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

Status: **implemented in the Phase 7 package as an experimental device path**

Implemented:

1. virtio-style in-kernel simulated accelerator queue
2. x86 PCI configuration-space enumeration
3. experimental DMA-addressable buffer path under the current identity-map model
4. intentionally narrow accelerator backend ABI (`submit/kick/poll/reset`)
5. PCI accelerator/display candidates registered offline until a real driver exists

Still deliberately deferred:

- modern NVIDIA/AMD/Intel GPU driver
- BAR programming and MSI/MSI-X
- IOMMU-backed isolation
- production PCIe ECAM/MCFG support

See `docs/PHASE7_REAL_DEVICE_PATH.md`.

## Milestone 8 — Inference-oriented experiments

Status: **implemented in the restored Phase 8 package**

Implemented research control-plane primitives:

- persistent model objects and residency state
- KV-cache/session residency metadata
- deadline-aware request queues and dynamic batching
- tensor prefetch planning
- memory-pressure-aware admission control
- explicit inference memory-budget accounting

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
