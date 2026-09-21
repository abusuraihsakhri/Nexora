# Phase 7 Integration Notes

This package was built from the available `aikernel-starter` baseline. If the active Nexora tree already contains completed Phases 1–6, merge Phase 7 at the interfaces below rather than replacing newer subsystems.

## 1. Memory-manager integration

Replace the experimental identity-map assumption in `src/mm/dma.c` with the existing physical/virtual memory manager.

Desired final interface:

```text
DMA allocation request
    ↓
physical page allocator
    ↓
contiguous/scatter-gather selection
    ↓
IOMMU mapping (when available)
    ↓
device IOVA + CPU mapping
```

Do not preserve `(u64)virt` as the long-term DMA address rule.

## 2. User-mode/syscall integration

If Phase 5/6 already exposes user mode and AI syscalls, keep device programming in the kernel and expose only narrow operations such as:

```text
ai_device_query
ai_work_submit
ai_work_wait
```

Do not expose raw PCI config writes or arbitrary BAR mappings to untrusted AI workloads.

## 3. Capability integration

Before a non-simulator backend accepts work, gate dispatch on the existing capability model:

```text
subject may execute
AND
subject may use target accelerator
AND
memory handles grant required access
AND
quota/deadline policy allows admission
```

Phase 7's demo does not yet thread a capability token through every submission because its purpose is device-path validation.

## 4. Shared-tensor integration

If Phase 4 already implements shared tensor handles, the accelerator command should reference validated tensor/backing handles rather than raw physical addresses supplied by user space.

Preferred flow:

```text
user tensor handle
    ↓
kernel resolves backing object
    ↓
capability check
    ↓
DMA/IOMMU mapping
    ↓
accelerator descriptor
```

## 5. Scheduler integration

The current demonstration selects the first online backend compatible with a work node's device mask.

If Phase 3 contains the full cost model, device selection should use its placement score:

```text
transfer cost
+ queue delay
+ execution estimate
+ memory pressure penalty
+ deadline penalty
```

The Phase 7 registry is intended to supply the concrete device candidates for that algorithm.

## 6. PCI evolution

The next hardware step should add, in order:

1. ACPI table access;
2. MCFG/ECAM support;
3. BAR sizing and mapping;
4. MSI/MSI-X;
5. DMA masks and IOMMU mapping;
6. one intentionally simple experimental accelerator protocol.

A modern NVIDIA/AMD/Intel GPU driver should still not be the immediate next task.

## 7. Real-device backend contract

A real backend should remain behind the existing four-method ABI:

```c
submit()
kick()
poll()
reset()
```

Device-specific details should stay private to the driver. This is what allows the AI scheduler, tensor manager, and capability system to remain hardware-independent.
