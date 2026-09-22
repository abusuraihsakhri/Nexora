# Nexora-RK architecture

## 1. Current boot path

```text
GRUB / Multiboot2
        │
        ▼
32-bit protected-mode entry
        │
        ▼
minimal identity page tables
        │
        ▼
x86-64 long mode
        │
        ▼
kmain()
        │
        ├── console + diagnostics
        ├── early/frame/slab allocators
        ├── GDT/TSS + IDT
        ├── syscall MSRs + per-CPU boundary state
        ├── Nexora backend bridge
        ├── tensor/work semantic layer
        ├── integration self-test
        └── demonstration graph
```

The current boot path remains kernel-only after initialization. Ring-3 ELF loading and syscall behavior are exercised by the hosted integration suite; production user-process launch requires a real user VM mapping layer.

## 2. Architectural split

Nexora separates **semantic policy inputs** from low-level **mechanisms**.

```text
Applications / runtimes
        │
        ▼
Nexora ABI
        │
┌───────┴───────────────────────────────────────────┐
│ Semantic plane                                   │
│ tensor metadata · work graph · state · rights    │
└───────┬───────────────────────────────────────────┘
        │
        ▼
Nexora scheduler / placement policy
        │
┌───────┴───────────────────────────────────────────┐
│ Mechanism plane                                  │
│ CPU context · VM · interrupts · DMA · drivers    │
└───────┬───────────────────────────────────────────┘
        │
        ▼
resources / topology
```

The v0.1 implementation contains only part of this model. In particular, heterogeneous device placement is represented semantically but not backed by real accelerator execution.

## 3. Core objects

### Tensor

A tensor currently records type, shape, size, semantic flags, location class, identity, and a kernel reference count.

The critical distinction is between **metadata** and **backing storage**. Current tensor objects are metadata objects; a future VM-backed storage object must own physical pages/device memory and mapping rights.

### Work node

A work node records an operation class, dependencies, priority, deadline, device mask, and input/output tensors. Work nodes retain their tensor dependencies so handle release cannot invalidate in-flight work.

Current execution is deterministic and synchronous. The graph/scheduler interfaces are intended to survive replacement by an asynchronous executor.

### Handle and capability boundary

Userspace-visible resources are represented by generation-tagged handles carrying rights. Delegation:

```text
source handle
   │  resolve + require DELEGATE
   ▼
retain object
   │
   ▼
allocate target handle with subset of source rights
```

Object retain/release is mandatory. Delegation never transfers a raw kernel pointer.

### Observability

Phase 16/17 modules are compiled into the active kernel:

- bounded telemetry;
- deterministic fault injection;
- release-health aggregation;
- structured trace ring;
- watchdog targets;
- immutable build identity.

They use fixed-capacity storage and do not allocate dynamically.

## 4. Safety boundary

v0.1 is UP-only. Global allocators and registries are therefore not advertised as SMP-safe. The active QEMU configuration uses one CPU.

Unrecoverable kernel exceptions are terminal. User faults are marked against the current process in host validation; on bare metal, the kernel stops rather than returning into a known-faulting context until a scheduler/process-exit path exists.

Tensor mapping is deliberately unavailable instead of exposing kernel addresses.

## 5. Next mechanisms

The highest-value next mechanisms are:

1. monotonic timer source;
2. user address-space creation and teardown;
3. VM-backed tensor storage/mapping;
4. asynchronous simulated accelerator queues;
5. graph-derived lifetime accounting;
6. topology and transfer-cost model;
7. SMP only after ownership and synchronization are designed explicitly.
