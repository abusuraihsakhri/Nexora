# AIKernel v0 Architecture

## Current boot path

```text
GRUB / Multiboot2
        |
        v
32-bit protected-mode entry
        |
        v
minimal page tables
        |
        v
x86-64 long mode
        |
        v
kmain()
        |
        +--> console
        +--> early allocator
        +--> kernel object registry + ownership/refcounts
        +--> synthetic CPU/GPU/NPU device topology
        +--> memory-object system
        +--> tensor registry + strong backing references
        +--> automatic reclamation manager
        +--> work graph
        +--> scheduler
        +--> capability prototype
```

## Proposed long-term architecture

```text
+------------------------------------------------------+
| Applications / agents / inference services          |
+--------------------------+---------------------------+
                           |
                           v
+------------------------------------------------------+
| AI Runtime ABI                                       |
| work submission | tensor handles | capabilities     |
+--------------------------+---------------------------+
                           |
                           v
+------------------------------------------------------+
| Work Graph Manager                                  |
| dependencies | deadlines | priorities | provenance  |
+--------------------------+---------------------------+
                           |
             +-------------+-------------+
             |                           |
             v                           v
+------------------------+    +------------------------+
| AI Scheduler           |    | Tensor Memory Manager  |
| CPU/GPU/NPU placement  |    | HBM/RAM/NVMe/remote   |
+-----------+------------+    +-----------+------------+
            |                             |
            +-------------+---------------+
                          |
                          v
+------------------------------------------------------+
| Capability + isolation layer                        |
+--------------------------+---------------------------+
                           |
                           v
+------------------------------------------------------+
| Device/resource model                               |
| CPU | GPU | NPU | NIC | NVMe | remote accelerator |
+------------------------------------------------------+
```

## Core kernel objects

### Tensor

A tensor is a typed multidimensional data object whose semantics are separate from
its storage. It now binds to `NX_OBJECT_MEMORY` through a generation-checked handle
and optional byte offset.

Implemented storage/lifetime fields include:

- backing memory handle
- backing offset/capacity
- page-granular resident backing through the bootstrap early-heap backend
- explicit object owner ID
- strong reference count
- pin count
- deterministic destructor callback
- tensor-owned strong references to backing memory
- lifetime class and residency state
- producer work handle
- consumer work handles, completion bitmask, and remaining-consumer count

Important future fields:

- arbitrary virtual mappings
- reuse distance hint
- real hardware-discovered NUMA/device topology
- compression/quantization state

### Work node

Represents schedulable computation. Phase 3 Step 7 upgrades it into an AI-native
execution object rather than a generic task record.

Implemented fields now include:

- operation type and independent work class
- QoS class
- explicit control dependencies
- producer/consumer-derived tensor dependencies
- strong input/output tensor references
- priority and optional deadline
- allowed device-class mask
- estimated operation count
- estimated read/write traffic
- estimated scratch storage
- estimated execution duration
- exact logical input/output tensor bytes
- batch identity and batch size
- preemptible/batchable/deterministic/idempotent declarations

The scheduler consumes priority, deadline presence/value, QoS, duration metadata, and Step 8 placement data. A work object can now carry an optional concrete preferred device and records the concrete selected device used for dispatch. Allowed-device masks remain hard constraints.

### Device/locality model

Phase 3 Step 8 introduces concrete `NX_OBJECT_DEVICE` instances and a synthetic reference topology. The current boot topology contains one CPU, one GPU, and one NPU. Their compute rates, memory bandwidths, capacities, and inter-device links are model parameters used for research tests; they are **not hardware discovery results**.

Each tensor now tracks:

- `preferred_device` — desired storage/locality target inferred from its logical location unless overridden
- `resident_device` — concrete device currently associated with its backing

The placement model evaluates every allowed concrete device. For a candidate device, Nexora estimates:

```text
transfer_cost = sum(input transfers) + sum(output transfers)
execution_cost = max(compute-time estimate, memory-time estimate)
total_cost = transfer_cost + execution_cost
```

Input transfers are charged when an input's resident/preferred device differs from the candidate. Output transfers are charged when the candidate differs from the output tensor's preferred/resident destination. Direct links carry bandwidth and fixed-latency parameters. A missing direct route makes that placement infeasible.

Preferred work devices are a deterministic tie-break when total cost is equal; they do not inject an arbitrary hidden penalty into the numeric cost. Scheduler intent still dominates placement: priority, deadline, QoS, and supplied duration ordering are considered before locality cost when choosing between different work nodes.

The current system only **models** device transfers. It does not yet execute DMA or copy bytes between actual accelerators. Non-CPU tensor backing remains physically provided by the bootstrap CPU arena and marked as emulated device memory. See `PHASE3_STEP8_LOCALITY_DEVICE_PLACEMENT.md`.

### Capability

Describes permission to operate on resources.

Future direction:

```text
capability = subject + object + rights + constraints
```

Example constraints:

- tensor range
- model identifier
- time limit
- accelerator quota
- network destination

## What v0 deliberately omits

- interrupts/IDT
- APIC
- SMP startup
- user mode
- syscalls
- filesystem
- networking
- PCI enumeration
- real GPU driver
- model runtime
- general physical page allocator / reclaimable tensor backing
- real device-memory allocator

Those should be added only after the architectural experiments are specified.


## Tensor lifetime policy

Phase 3 Step 5 makes lifetime a semantic property rather than an allocator side effect.
`TEMPORARY`, `PERSISTENT`, `CACHED`, `SHARED`, and `EXTERNAL` tensors use different
reclaim rules. The lifetime engine may detach backing while preserving the tensor object,
which allows graph-aware early reclamation and cache eviction without losing tensor identity.
Pins and shared strong references constrain reclamation. See
`PHASE3_STEP5_TENSOR_LIFETIME_ENGINE.md`.

## Producer/consumer dataflow

Phase 3 Step 6 makes dataflow provenance kernel-visible. Every tensor can have one
non-owning producer work handle and up to 32 unique non-owning consumer handles.
Work nodes hold the strong references to attached tensors, so the model does not
create reference-count cycles.

```text
Work producer
      |
      v
    Tensor
    /    \
   v      v
Work A  Work B
```

The scheduler treats a tensor producer as an implicit data dependency. A consumer
cannot become `READY` until the producer is `DONE`, even when no explicit work-ID
dependency was added.

Consumer completion decrements `consumers_remaining` exactly once. When the count
reaches zero, the graph emits a final-consumer event. For resident temporary
tensors, Step 9 routes that event through the reclamation manager. Immediate
reclaim is attempted first; retryable failures such as transient object pins are
queued by generation-checked tensor handle and retried at later scheduler/pressure
points. Tensor metadata and identity survive backing reclaim.

The same manager exposes a target-based pressure pass. It conservatively considers
only graph-dead temporary tensors and safe cached tensors, prioritizes temporary
data before cache eviction, and stops only after the requested number of bytes have
actually left live resident-memory accounting or no eligible candidate remains.
Persistent, external, pinned, and graph-live tensors are protected. The bootstrap
early heap is still monotonic, so this is logical/live residency reclamation; the
underlying pages are not reusable until a real page-frame allocator replaces that
backend. See `PHASE3_STEP9_AUTOMATIC_RECLAMATION.md`.

## AI work-object execution metadata

Phase 3 Step 7 separates graph topology from execution cost metadata. Tensor edges
express dependencies and exact logical I/O volume; the work descriptor expresses
expected computational/resource behavior. This gives later placement policy both sides
of the problem without pretending that tensor size is identical to hardware traffic.

With `AI_WORK_FLAG_AUTO_IO_BYTES`, logical input/output sizes seed read/write estimates.
Explicit estimates may instead be supplied for operators whose actual traffic differs
from the logical edge volume. Batch and QoS metadata are stored but do not yet trigger
dynamic batching. See `PHASE3_STEP7_AI_WORK_OBJECT_REDESIGN.md`.


## Instrumentation and Phase 3 measurement

Phase 3 Step 10 adds a fixed-capacity, allocation-free trace buffer. Tensor creation/binding,
work creation/dispatch/completion, backing unbind, reclamation, deferral, and explicit memory
samples can be recorded together with the current live `resident_bytes` count. Debug helpers
dump the tensor registry, work graph, memory summary, and trace over the kernel console.

The host benchmark uses the same three-stage graph twice. The baseline disables only automatic
final-consumer reclamation; the graph-aware condition leaves it enabled. Outputs are allocated
lazily at producer dispatch. In the bundled synthetic workload the measured peak live residency
is 608 KiB for the baseline and 448 KiB for the graph-aware path, a 160 KiB (26.31%) reduction.

This is evidence for the object/lifetime model under a controlled workload, not yet a physical
page-reuse result. `NX_MEMORY_BACKEND_EARLY_HEAP` is monotonic, so logical destruction reduces
live-resident accounting without returning pages to a reusable allocator. See
`PHASE3_STEP10_INSTRUMENTATION_DEMO.md`.
