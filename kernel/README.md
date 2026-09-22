# Nexora-RK

Nexora-RK is the bare-metal research-kernel implementation of **NEXORA — Neural EXecution Orchestration and Resource Architecture**.

It exists to test one narrow research hypothesis: whether exposing AI workload semantics to the operating-system resource manager can improve scheduling, memory use, locality, isolation, and accelerator utilization.

## Current scope

The current kernel is an **x86-64, QEMU-first, uniprocessor research system**. It is intentionally small and explicit about what is simulated.

Implemented and build-validated surfaces include:

- Multiboot2 boot through GRUB and transition to x86-64 long mode;
- early paging, VGA/serial diagnostics, and panic handling;
- frame and slab allocation for kernel objects;
- GDT/TSS, IDT, syscall MSRs, and a capability-aware syscall dispatch layer;
- ELF64 structural validation and host-side Ring-3 ABI tests;
- typed tensor metadata with overflow checks and reference-counted ownership;
- work graphs, dependency validation, deterministic scheduling, and deadlock reporting;
- generation-tagged handles and constrained capability delegation;
- Phase 16 telemetry, fault injection, and release-health modules;
- Phase 17 health, trace, watchdog, and build-information modules;
- host regression tests, sanitizers, and headless QEMU boot validation in CI.

## Deliberate limits

Nexora-RK v0.1 does **not** currently claim:

- SMP safety or application-processor startup;
- real GPU/NPU kernel execution;
- a production virtual-memory subsystem for user processes;
- tensor userspace mapping — `NEXORA_SYS_AI_TENSOR_MAP` returns `ENOSYS` until a real VM-backed mapping exists;
- production Ring-3 process launch from the boot path;
- preemptive/asynchronous device scheduling;
- calibrated kernel timestamps where no monotonic clock source has been wired;
- physical-page reclamation from fully free slab pages.

These are engineering boundaries, not hidden TODOs. Measurements and claims should be interpreted within them.

## Build and verify

On Debian/Ubuntu-like systems:

```bash
sudo apt update
sudo apt install build-essential gcc binutils grub-pc-bin grub-common \
  xorriso qemu-system-x86 gdb
```

Run the active verification gate:

```bash
make verify-current
```

Build the bootable image:

```bash
make
```

Run in QEMU:

```bash
make run
```

The v0.1 QEMU configuration deliberately uses one virtual CPU.

## Source layout

```text
include/
  ai/                 semantic AI objects + qualification APIs
  kernel/             low-level kernel interfaces
  nexora/             syscall ABI, handles, processes, Phase 17 interfaces
src/
  ai/                 tensors, work graphs, scheduler, telemetry
  arch/x86_64/        boot, GDT/TSS, IDT, syscalls, user transition
  kernel/             syscall/process/backend/ELF mechanisms
  mm/                 early, frame, and slab allocators
  phase17/            health, tracing, watchdog, build identity
tests/                hosted regression/integration tests
docs/                 architecture, research roadmap, limitations
```

## Research direction

The next kernel work should prioritize mechanisms needed to test Nexora's hypotheses, not general-purpose OS breadth:

1. a real monotonic clock and measured scheduling latency;
2. VM-backed tensor storage and protected userspace mappings;
3. explicit graph-derived tensor lifetime reclamation;
4. a simulated/virtio-style accelerator with asynchronous completion;
5. topology-aware placement;
6. only after those are stable, SMP and real accelerator backends.

See `docs/ARCHITECTURE.md`, `docs/KNOWN_LIMITATIONS.md`, and the repository root specification.
