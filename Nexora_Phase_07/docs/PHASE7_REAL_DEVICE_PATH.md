# Nexora Phase 7 — Real Device Path

## Status

**Implemented as an experimental device-path milestone.**

This phase follows the original roadmap progression:

1. virtio-style simulated accelerator;
2. PCI enumeration;
3. simple DMA-capable experimental path;
4. narrow accelerator backend interface;
5. defer modern production GPU drivers until the abstraction is validated.

The word *real* in this milestone means that Nexora now crosses the kernel/device boundary through explicit device discovery, DMA-addressable buffers, queue submission, completion, and backend registration. It does **not** mean that CUDA, ROCm, oneAPI, or a vendor GPU kernel driver exists in Nexora.

---

## Phase 7 objective

Create the minimum hardware-facing substrate needed to test whether the AI-native scheduler and object model can drive heterogeneous accelerators without coupling the kernel to a specific vendor stack.

The central rule is:

> The scheduler sees an accelerator contract, not a GPU vendor API.

---

## Step 1 — Architecture I/O primitives

Added `include/arch/x86_64/io.h` with freestanding x86-64 port-I/O helpers and an explicit memory fence.

Implemented primitives:

- 8/16/32-bit port reads;
- 8/16/32-bit port writes;
- device/CPU ordering fence.

These primitives are deliberately small and architecture-local.

---

## Step 2 — PCI configuration-space enumeration

Added:

- `include/kernel/pci.h`
- `src/drivers/pci.c`

The implementation uses x86 PCI Configuration Mechanism #1 (`0xCF8/0xCFC`) and records:

- BDF: bus/device/function;
- vendor/device IDs;
- class/subclass/programming interface;
- revision;
- header type;
- six BAR values;
- IRQ line/pin.

Phase 7 identifies two accelerator-relevant PCI classes:

- class `0x12`: Processing Accelerator;
- class `0x03`: Display Controller.

### Important limitation

This phase does not yet parse ACPI MCFG or PCIe ECAM segments. It is therefore an experimental x86 path, not a complete PCIe implementation.

---

## Step 3 — Experimental DMA buffers

Added:

- `include/kernel/dma.h`
- `src/mm/dma.c`

The DMA API provides:

```c
dma_alloc()
dma_zero()
dma_sync_for_device()
dma_sync_for_cpu()
```

`dma_buffer` carries both CPU virtual and device-visible physical addresses.

### Current translation model

The starter kernel identity-maps low memory, so Phase 7 currently uses:

```text
physical address == kernel virtual address
```

for buffers allocated from the early heap.

This is valid only for the current experimental memory model. A later integration with the Phase 1/2 physical/virtual memory managers should replace this with a canonical `virt_to_phys()`/DMA mapping service.

### Security limitation

No IOMMU isolation exists yet. A production accelerator path must not treat this allocator as safe for mutually untrusted devices or processes.

---

## Step 4 — Narrow accelerator backend ABI

Added:

- `include/ai/accelerator.h`
- `src/ai/accelerator.c`

A backend exposes only four operations:

```c
submit()
kick()
poll()
reset()
```

The generic device descriptor carries:

- transport type;
- AI device mask;
- PCI identity when applicable;
- DMA address width;
- queue depth;
- online/offline state;
- counters for submission, completion, failures, and bytes moved.

This contract is intentionally much narrower than CUDA/HIP/OpenCL/Vulkan. Those APIs may eventually exist in user space or a compatibility/runtime layer, but they are not the Nexora kernel ABI.

---

## Step 5 — Virtio-style in-kernel accelerator simulator

Added:

- `include/drivers/nex_accel_sim.h`
- `src/drivers/nex_accel_sim.c`

The simulator provides a fixed-depth descriptor queue with:

- producer index;
- consumer index;
- pending-count accounting;
- explicit `kick` and `poll` semantics;
- completion states.

It supports three command types:

```text
NOP
COPY
WORK
```

`COPY` performs a real CPU-visible buffer copy through the DMA-address abstraction. `WORK` deliberately returns `SIMULATED`, not `OK`, because Phase 7 validates transport and scheduling semantics rather than pretending to execute a neural network kernel.

---

## Step 6 — PCI accelerator candidate registration

Added:

- `include/drivers/pci_accel.h`
- `src/drivers/pci_accel.c`

Discovered PCI processing accelerators and display controllers are registered in the accelerator registry as **offline/inert candidates**.

This is a deliberate safety mechanism. Device discovery alone is not sufficient evidence that Nexora knows how to program that device.

The kernel records its identity but does not touch BARs, DMA engines, firmware queues, or vendor command processors.

---

## Step 7 — Scheduler-to-device dispatch demonstration

The kernel boot demo now performs:

```text
AI work graph
    ↓
Nexora scheduler
    ↓
find compatible accelerator backend
    ↓
translate work node to accelerator command
    ↓
submit/kick/poll/wait
    ↓
completion state
    ↓
mark work node done/failed
```

On the simulated backend, AI work returns `SIMULATED` and is considered a successful path-validation completion.

This validates the control plane while preserving a strict distinction between scheduling and actual numerical execution.

---

## Step 8 — DMA data-path self-test

At boot, Phase 7 allocates two 256-byte DMA buffers, fills the source with a deterministic pattern, submits a `COPY` command, waits for completion, synchronizes for CPU access, and verifies every byte.

Expected result:

```text
DMA command status: OK
DMA copy verification: PASS
```

This tests more than a function call: it exercises allocation, device-address construction, submission, queue processing, completion, synchronization, and verification.

---

## Step 9 — Kernel-visible telemetry

Each accelerator records:

```text
submitted
completed
failed
bytes_moved
```

The boot path prints an accelerator summary. These counters are intentionally primitive; later phases can expose them through the syscall ABI, tracing, or a metrics ring.

---

## Step 10 — Build and validation gate

The Makefile now provides:

```bash
make phase7-check
```

The target:

1. compiles every Phase 7 source with the kernel's strict warnings-as-errors configuration;
2. links `build/kernel.elf`;
3. checks that required Phase 7 symbols are present.

Required symbols include:

```text
pci_enumerate
dma_alloc
ai_accel_register
nex_accel_sim_init
pci_accel_discover
```

---

# New execution architecture

```text
                     +-----------------------+
                     | AI work graph         |
                     +-----------+-----------+
                                 |
                                 v
                     +-----------------------+
                     | Nexora scheduler      |
                     +-----------+-----------+
                                 |
                                 v
                     +-----------------------+
                     | Accelerator registry  |
                     +----+-------------+----+
                          |             |
              online      |             | discovered/offline
                          v             v
             +----------------+   +-----------------------+
             | sim backend    |   | PCI candidate backend |
             | queue + DMA    |   | identity only         |
             +-------+--------+   +-----------+-----------+
                     |                        |
                     v                        v
             +----------------+       +---------------+
             | DMA buffers    |       | PCI config    |
             | identity map   |       | enumeration   |
             +----------------+       +---------------+
```

---

# Acceptance criteria

Phase 7 is accepted when all of the following are true:

- [x] Kernel has architecture-local port I/O primitives.
- [x] PCI devices can be enumerated through configuration mechanism #1.
- [x] Accelerator-relevant PCI classes can be identified.
- [x] DMA-addressable buffers can be allocated under the current identity-map model.
- [x] Generic accelerator backend ABI exists.
- [x] Simulated accelerator uses queue submission/completion semantics.
- [x] DMA copy path has deterministic verification logic.
- [x] Scheduler work can traverse the generic accelerator interface.
- [x] Physical PCI candidates are not treated as online without a driver.
- [x] Accelerator telemetry is recorded.
- [x] Kernel compiles and links with warnings treated as errors.
- [x] Phase 7 required symbols are checked automatically.
- [ ] Runtime QEMU boot test performed in an environment with QEMU + GRUB tooling.
- [ ] Real BAR mapping and device-specific command queue implemented.
- [ ] IOMMU-backed DMA isolation implemented.

The last three items are intentionally outside the verified portion of this package.
