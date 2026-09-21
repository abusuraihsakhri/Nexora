# Nexora AIKernel Starter

A small **x86-64 experimental research kernel** intended as a foundation for exploring AI-native operating-system abstractions.

This is deliberately **not a Linux clone**. The initial design treats these as first-class concepts:

- tensors and their placement/lifetime metadata
- AI work units and dependency graphs
- heterogeneous compute targets (CPU/GPU/NPU)
- capability-based access control
- deadline/priority-aware scheduling
- data locality as an explicit concern

The current version is a bootable research skeleton. It does **not** yet run real GPU/NPU kernels.

## What already works

- Multiboot2 boot through GRUB
- transition from 32-bit GRUB entry to x86-64 long mode
- 1 GiB identity mapping using 2 MiB pages
- VGA text output
- COM1 serial output
- panic primitive
- tiny bump allocator
- generation-checked kernel object registry with owner IDs, strong refs, pins, and destructors
- tensor metadata with classes, shapes, byte strides, overflow-safe sizing, and layout tracking
- `NX_OBJECT_MEMORY` with page-aligned/page-rounded bootstrap backing
- tensor-to-memory strong-reference binding, offset views, bounds checking, pinning, and resident-byte accounting
- tensor lifetime engine with temporary/persistent/cached/shared/external policies and policy-aware backing reclamation
- producer/consumer tensor graph with one-producer/multi-consumer provenance
- scheduler data dependencies derived directly from tensor producer relationships
- final-consumer accounting with automatic temporary-tensor reclamation and deferred retry
- pressure-target reclamation of graph-dead temporary tensors and safe cached tensors
- cache-only eviction service with actual live resident-byte accounting
- AI-native work descriptors with work class, QoS, cost estimates, batch identity, and execution flags
- automatic logical tensor-I/O byte accounting for work nodes
- scheduler handling for explicit no-deadline semantics plus priority/deadline/QoS/duration ordering
- first-class synthetic CPU/GPU/NPU device objects with concrete generation-checked identities
- tensor preferred/resident device tracking tied to tensor storage location
- direct-link transfer latency/bandwidth model and overflow-safe transfer-time estimates
- locality-aware placement evaluation across allowed device classes
- placement choice combining execution estimate, input/output transfer cost, and preferred-device tie breaking
- scheduler dispatch choices that bind a work node to a concrete selected device
- capability objects
- simple priority/dependency scheduler
- demonstration graph created during kernel boot

## Project layout

```text
aikernel-starter/
├── Makefile
├── linker.ld
├── grub.cfg
├── include/
│   ├── ai/
│   │   ├── capability.h
│   │   ├── device.h
│   │   ├── lifetime.h
│   │   ├── reclaim.h
│   │   ├── scheduler.h
│   │   ├── tensor.h
│   │   └── work.h
│   └── kernel/
│       ├── memory.h
│       ├── memory_object.h
│       ├── panic.h
│       ├── printk.h
│       └── types.h
├── src/
│   ├── ai/
│   ├── arch/x86_64/
│   ├── kernel/
│   └── mm/
├── docs/
│   ├── ARCHITECTURE.md
│   ├── DESIGN_PRINCIPLES.md
│   └── ROADMAP.md
└── scripts/
    ├── debug.sh
    └── run.sh
```

## Recommended build environment

A Debian/Ubuntu/Kali-like Linux system or WSL2.

Install:

```bash
sudo apt update
sudo apt install build-essential gcc binutils grub-pc-bin grub-common \
    xorriso qemu-system-x86 gdb
```

## Build

```bash
make
```

The build creates:

```text
build/kernel.elf
build/aikernel.iso
```

## Run in QEMU

```bash
make run
```

or:

```bash
./scripts/run.sh
```

Expected serial output includes:

```text
AIKernel x86_64 booted.
Early heap initialized.
Kernel object registry initialized.
Kernel object self-test passed.
AI device/locality model initialized.
AI device/locality model self-test passed.
Kernel memory-object system initialized.
Memory ownership/refcount self-test passed.
AI runtime metadata initialized.
Tensor metadata/backing/refcount self-test passed.
Tensor lifetime-engine self-test passed.
Automatic reclamation self-test passed.
Producer/consumer graph self-test passed.
AI work-object redesign self-test passed.
Locality/device placement self-test passed.

[Nexora Phase 3 instrumentation demo]
tensor input ... class=input ... layout=contiguous ...
tensor weights ... class=weight ... layout=contiguous ...
scheduler selected node ...
[Nexora work graph]
[Nexora tensor table]
[Nexora memory summary]
[Nexora trace]
...
AIKernel initialization complete.
```


## Phase 3 instrumentation benchmark

Run the complete host suite with:

```bash
make test
```

Or run only the Phase 3 A/B memory experiment:

```bash
make test-instrument
```

The bundled controlled experiment currently reports:

```text
baseline peak live resident:    608 KiB
graph-aware peak live resident: 448 KiB
reduction:                      160 KiB (26.31%)
```

This is live-residency accounting, not reusable physical-page reclamation: the bootstrap early
heap remains monotonic. See `docs/PHASE3_STEP10_INSTRUMENTATION_DEMO.md`.

## Debug

Terminal 1:

```bash
make debug
```

Terminal 2:

```bash
gdb build/kernel.elf
(gdb) target remote :1234
(gdb) break kmain
(gdb) continue
```

## Clean

```bash
make clean
```

## Research direction

The first serious experiments should compare an AI-native primitive with a conventional Linux implementation. Good early candidates:

1. tensor-aware allocator vs generic allocation
2. graph scheduler vs independent task queue
3. explicit tensor lifetime reclamation
4. topology-aware placement simulation (initial in-kernel model now implemented)
5. zero-copy shared tensor handles
6. capability-based agent resource access

Do **not** attempt GPU-driver development first. Prove one architectural advantage before expanding hardware support.

See `docs/ROADMAP.md`, `docs/PHASE3_STEP1_OBJECT_FOUNDATION.md`, `docs/PHASE3_STEP2_TENSOR_OBJECT.md`, `docs/PHASE3_STEP3_TENSOR_PHYSICAL_BACKING.md`, `docs/PHASE3_STEP4_OWNERSHIP_REFERENCE_COUNTING.md`, `docs/PHASE3_STEP5_TENSOR_LIFETIME_ENGINE.md`, `docs/PHASE3_STEP6_PRODUCER_CONSUMER_GRAPH.md`, and `docs/PHASE3_STEP7_AI_WORK_OBJECT_REDESIGN.md`, plus `docs/PHASE3_STEP8_LOCALITY_DEVICE_PLACEMENT.md`, `docs/PHASE3_STEP9_AUTOMATIC_RECLAMATION.md`, and `docs/PHASE3_STEP10_INSTRUMENTATION_DEMO.md`.
