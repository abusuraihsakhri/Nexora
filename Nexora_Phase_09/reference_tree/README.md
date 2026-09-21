# AIKernel Starter

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
- tensor registry
- work-graph representation
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
│   │   ├── scheduler.h
│   │   ├── tensor.h
│   │   └── work.h
│   └── kernel/
│       ├── memory.h
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
AI runtime metadata initialized.

[AIKernel demo]
tensor input: ...
tensor weights: ...
work node 1: READY
work node 2: BLOCKED
scheduler selected node 1
...
AIKernel initialization complete.
```

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
4. topology-aware placement simulation
5. zero-copy shared tensor handles
6. capability-based agent resource access

Do **not** attempt GPU-driver development first. Prove one architectural advantage before expanding hardware support.

See `docs/ROADMAP.md`.
