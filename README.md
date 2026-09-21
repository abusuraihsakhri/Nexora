# NEXORA

## Neural EXecution Orchestration and Resource Architecture

<div align="center">

![Architecture](https://img.shields.io/badge/Architecture-x86__64%20Microkernel-blue.svg?style=for-the-badge)
![Specification](https://img.shields.io/badge/Spec-v0.1%20Research%20Draft-purple.svg?style=for-the-badge)
![Security Audit](https://img.shields.io/badge/OWASP%202025-100%25%20Remediated-brightgreen.svg?style=for-the-badge)
![Documentation](https://img.shields.io/badge/Docs-GitHub%20Pages-blueviolet.svg?style=for-the-badge)
![Privilege](https://img.shields.io/badge/Isolation-Ring%200%20%7C%20Ring%203-orange.svg?style=for-the-badge)
![License](https://img.shields.io/badge/License-MIT-lightgrey.svg?style=for-the-badge)

<p align="center">
  <b>An operating-system architecture in which AI workload semantics participate directly in resource management.</b>
</p>

[Interactive Docs & Simulator](https://abusuraihsakhri.github.io/Nexora/) • [Executive Definition](#1-executive-definition) • [Core Primitives](#5-the-eight-core-nexora-primitives) • [Kernel Architecture](#6-kernel-architecture) • [Security Hardening](#12-security-audit--owasp-2025-remediation-matrix) • [Kernel Unification](#13-kernel-unification-milestones-m1m6) • [Building & Testing](#16-building--testing)

</div>

---

# 1. Executive Definition

**Nexora is an AI-native operating-system architecture designed around semantic knowledge of AI workloads rather than conventional process/page/device abstractions alone.**

### Central Hypothesis

> *An operating system that understands computation graphs, tensor lifetimes, model state, accelerator requirements, topology, data locality, deadlines, and resource capabilities can manage AI workloads more efficiently than an operating system that sees primarily processes, virtual pages, files, and devices.*

Nexora does **not** attempt to replace PyTorch, JAX, CUDA, ROCm, OpenXLA, Kubernetes, or existing model-serving frameworks initially. Instead, Nexora introduces an OS-level semantic resource-management layer underneath or alongside them:

```text
               AI Applications / Agents
                         │
                  PyTorch / JAX
                         │
              Nexora Runtime Adapter
                         │
                  Nexora AI ABI
                         │
┌───────────────────────────────────────────────────┐
│                 NEXORA KERNEL                     │
│                                                   │
│  Work Graph       Tensor/State       Capability   │
│      │                 │                 │        │
│      └────────────┬────┴──────────┬─────┘        │
│                   │               │               │
│            Global Scheduler    Placement          │
│                   │               │               │
│              Resource Fabric / Topology           │
└───────────────────────┬───────────────────────────┘
                        │
          ┌─────────────┼──────────────┐
          │             │              │
         CPU           GPU            NPU
          │             │              │
         RAM           HBM         Accelerator RAM
          │             │              │
          └────── NVMe / NIC / Remote ─┘
```

---

# 2. Problem Statement & Information Gap

Contemporary AI stacks cross multiple isolated abstraction layers:

```text
Model server knows:  requests, models, KV caches, batches
Framework knows:     tensors, operation DAGs
Compiler knows:      compiled kernels, inter-op dependencies
GPU runtime knows:   hardware streams, command buffers
Linux knows:         processes, threads, virtual pages, file descriptors
Hardware knows:      execution queues, memory, execution units
```

No single layer possesses complete information about **what** is computed, **where** its data resides, **when** it will be reused, **which** accelerator should run it, **what** deadline applies, and **which** resource capabilities are authorized. Nexora exposes this semantic information directly to kernel resource managers.

---

# 3. Core Design Principles

1. **AI Semantics are First-Class Information:** The kernel explicitly differentiates model weights, activations, KV caches, temporary workspaces, dataset shards, checkpoints, and persistent state rather than treating all memory as anonymous pages.
2. **Data Movement is a Primary Cost:** Optimizes $\text{Compute Cost} + \text{Data Movement Cost}$ holistically. Moving 10 GB across PCIe/interconnects is often more expensive than the compute executed upon it.
3. **Heterogeneous Compute is Normal:** CPU, GPU, NPU, NIC, and storage appear within a single, unified resource and topology model.
4. **Work Dependencies Drive Resource Management:** Explicit knowledge of DAG flows ($A \to B \to C$) guides scheduling, prefetching, memory reclamation, and synchronization.
5. **Policy and Mechanism Remain Separate:** Mechanisms are kernel-level; scheduling policies (FIFO, deadline, locality, energy, LLM inference) remain interchangeable.
6. **Compatibility Over Purity:** Leverages existing ecosystems (CUDA, ROCm, PJRT, XLA, PyTorch, JAX, vLLM, SGLang) rather than recreating them.

---

# 4. Two-Implementation Strategy

Nexora evolves along two complementary research tracks:

```text
   +---------------------------------------+       +---------------------------------------+
   |              Nexora-RK                |       |               Nexora-X                |
   |        (Research Microkernel)         |       |      (Experimental Co-Kernel)         |
   +---------------------------------------+       +---------------------------------------+
   | - Bare-metal x86_64 / QEMU target     |       | - Linux-hosted co-kernel runtime      |
   | - Hardware privilege separation       |       | - Bridges PyTorch/JAX to accelerators |
   | - Frame allocator & slab reclamation  |       | - Hooks CUDA, ROCm, and PJRT backends |
   | - Custom IDT, TSS, and %gs syscalls   |       | - Large-scale production benchmarking |
   +---------------------------------------+       +---------------------------------------+
```

Mechanisms proven in Nexora-X migrate into the bare-metal Nexora-RK.

---

# 5. The Eight Core Nexora Primitives

| # | Primitive | Purpose & Semantic Definition |
|---|-----------|-------------------------------|
| **1** | `NXTensor` | Semantic data object with OS-visible dtype, shape, size, location (RAM/HBM), lifetime, mutability, reuse hints, and producer/consumer tracking. |
| **2** | `NXWork` | Schedulable semantic unit defining operation type, input/output tensors, dependency list, deadline, priority, and resource requirements. |
| **3** | `NXGraph` | Directed dependency computation graph allowing the OS to determine what can execute, what must wait, and what memory can be immediately freed. |
| **4** | `NXState` | Persistent semantic resources beyond ephemeral tensors (e.g., loaded model weights, KV caches, LoRA adapters, and persistent agent state). |
| **5** | `NXResource` | Unified representation making CPUs, GPUs, NPUs, HBM, RAM, NVMe, and NICs queryable through a single common resource model. |
| **6** | `NXTopology` | Hardware interconnect graph capturing latency, bandwidth, NUMA distance, PCIe hierarchy, and direct accelerator accessibility. |
| **7** | `NXCapability` | Unforgeable resource authority granting fine-grained execution, memory, model, and network delegation with explicit constraints. |
| **8** | `NXScheduler` | Multi-factor holistic scheduler evaluating execution time, transfer cost, queue delay, memory pressure, deadline penalties, and topology. |

### The Scheduler Cost Function

For candidate execution unit $D$ and work item $W$:

$$\text{Total Cost}(W, D) = \text{Time}_{\text{exec}} + \text{Cost}_{\text{transfer}} + \text{Delay}_{\text{queue}} + \text{Penalty}_{\text{pressure}} + \text{Penalty}_{\text{deadline}} + \text{Penalty}_{\text{topology}}$$

The lowest acceptable-cost placement wins.

---

# 6. Kernel Architecture: Semantic vs. Mechanism Plane

The kernel cleanly separates semantic understanding from low-level hardware virtualization:

```text
                  Applications & AI Agents
                             │
                      Nexora Runtime
                             │
                      Nexora AI ABI
                             │
        ┌────────────────────┴────────────────────┐
        │                                         │
 [ SEMANTIC PLANE ]                      [ MECHANISM PLANE ]
   - NXWorkGraph DAGs                      - CPU Scheduler & Timers
   - NXTensor Lifetimes                    - Physical Frame Allocator
   - NXState / Model Residency             - Virtual Memory & Paging
   - NXCapability Security                 - DMA & Device Drivers
   - NXTopology Placement                  - Interrupt Handlers (IDT)
        │                                         │
        └────────────────────┬────────────────────┘
                             │
                        NXScheduler
                             │
                        NXResource
                             │
               Heterogeneous Hardware Fabric
```

---

# 7. Initial Nexora ABI

The experimental system-call interface is purposefully minimal and semantic-centric:

```c
/* Tensor Management */
nx_tensor_create(const nx_tensor_desc_t *desc, nx_handle_t *out_handle);
nx_tensor_destroy(nx_handle_t tensor);
nx_tensor_map(nx_handle_t tensor, void **out_addr);
nx_tensor_unmap(nx_handle_t tensor);

/* Model & Persistent State */
nx_state_create(const nx_state_desc_t *desc, nx_handle_t *out_handle);
nx_state_release(nx_handle_t state);

/* Work Graph & Scheduling */
nx_graph_create(nx_handle_t *out_graph);
nx_graph_destroy(nx_handle_t graph);
nx_work_add(nx_handle_t graph, const nx_work_desc_t *work, nx_handle_t *out_node);
nx_work_submit(nx_handle_t graph, uint32_t flags);
nx_work_wait(nx_handle_t work_or_graph, uint64_t timeout_ns);

/* Resource & Capability Control */
nx_resource_query(nx_resource_query_t *query, nx_resource_info_t *info);
nx_cap_create(const nx_cap_desc_t *desc, nx_handle_t *out_cap);
nx_cap_delegate(nx_handle_t cap, uint32_t target_pid, nx_handle_t *out_handle);
nx_cap_revoke(nx_handle_t cap);
```

---

# 8. Competitive Boundaries & Non-Goals

| System | What It Does | Nexora's Strategic Separation |
|--------|--------------|-------------------------------|
| **Linux** | General-purpose OS (threads, pages, cgroups). | Nexora adds OS-level semantic AI resource awareness; does not reproduce full hardware drivers. |
| **LithOS** | Fine-grained GPU spatial scheduling. | Nexora targets whole-system (CPU + GPU + NPU + RAM + HBM + Fabric) semantic orchestration. |
| **NVIDIA Dynamo** | Generative-AI distributed inference router. | Dynamo acts at inference-routing level; Nexora operates below it as the host resource engine. |
| **NVIDIA Run:ai** | Cluster workload pooling and orchestration. | Run:ai manages cluster-level governance; Nexora manages machine-level execution and memory. |
| **OpenXLA / PJRT** | Framework-independent hardware device API. | Nexora integrates PJRT device backends directly into its semantic fabric. |
| **AIOS** | Agent scheduler running above the OS. | AIOS sits *above* host OS; Nexora provides the *underlying* capability-protected resource manager. |

### Explicit Non-Goals
* ❌ Do not build a proprietary GPU driver (leverage CUDA/PJRT/simulated backends).
* ❌ Do not recreate PyTorch or JAX (they run on top of Nexora).
* ❌ Do not build another ML compiler (leverage XLA, MLIR, Triton).
* ❌ Do not recreate Kubernetes (cluster orchestration is out of scope).
* ❌ Do not implement bloated POSIX compatibility.

---

# 9. Five Research Hypotheses & Target Workload

* **H1 — Semantic Tensor Lifetime:** Producer/consumer DAG tracking enables immediate reclamation of temporary tensors, reducing peak memory by $\ge 15\%$ over lifetime-blind allocation.
* **H2 — Locality-Aware Scheduling:** Graph and topology co-scheduling reduces avoidable host-to-device and device-to-device transfers by $\ge 20\%$.
* **H3 — Graph-Aware Scheduling:** Dependency-aware dispatch delivers lower p99 tail latency than independent FIFO scheduling under bursty inference.
* **H4 — Shared Tensor Handles:** Zero-copy capability-protected tensor sharing between isolated processes eliminates inter-process memory duplication.
* **H5 — Persistent Model State:** Treating model weights and KV caches as OS-managed persistent state measurably reduces cold-start latency and time-to-first-token (TTFT).

### Primary Target Workload
> **Multi-model LLM inference on a single heterogeneous machine** (concurrent dynamic requests, shared KV caches, high memory pressure, and CPU/GPU/NPU coordination).

---

# 10. The Four Project Rules

Every kernel abstraction introduced into Nexora must answer four mandatory questions:
1. *What existing problem does this solve?*
2. *Why can't the current application/runtime layer solve it adequately?*
3. *Why should the operating system know about it?*
4. *What measurable improvement results?*

---

# 11. Immediate Engineering Objective

The immediate priority is completing the semantic-memory loop:

$$\text{NXGraph} \longrightarrow \text{Producer/Consumer DAG} \longrightarrow \text{NXTensor} \longrightarrow \text{Semantic Lifetime} \longrightarrow \text{Physical Allocator} \longrightarrow \text{Auto Reclamation}$$

---

# 12. Security Audit & OWASP 2025 Remediation Matrix

Nexora underwent a comprehensive security audit evaluated against the **OWASP Top 10: 2025** threat model. All 7 identified vulnerabilities have been remediated, verified, and backed by automated regression tests:

| # | Vulnerability & Category | File Location | Security Impact | Remediation & Defensive Implementation | Test Status |
|---|--------------------------|---------------|-----------------|-----------------------------------------|-------------|
| **1** | **SYSRET Non-Canonical Return RIP**<br>`A10:2025 - Mishandling of Exceptional Conditions` | `Nexora_Phase_05/arch/x86_64/syscall_entry.S` | Non-canonical RIP in `sysretq` triggers `#GP` in Ring 0 with user stack active (CVE-2012-0217 class). | Validates bits 47..63 before `sysretq`; malformed addresses safely abort to kernel fault dispatch. | `PASS` |
| **2** | **Shared Global Syscall Stack in `.bss`**<br>`A06:2025 - Insecure Design` | `Nexora_Phase_05/arch/x86_64/syscall_entry.S` | Singleton user stack pointer clobbered under nested interrupts or multicore execution. | Moved `%rsp` storage to per-CPU structs addressed via `%gs:0` and `%gs:8`. | `PASS` |
| **3** | **ELF Loader Overlapping PT_LOAD Segments**<br>`A08:2025 - Software and Data Integrity Failures` | `Nexora_Phase_05/kernel/elf64.c` | Missed segment overlap check permitted mapping RW pages over executable RX memory ($W \oplus X$ bypass). | Implemented $O(N^2)$ pairwise interval overlap validation across all segments before mapping. | `PASS` |
| **4** | **Unhandled Page Fault in `uaccess`**<br>`A10:2025 - Mishandling of Exceptional Conditions` | `Nexora_Phase_05/kernel/uaccess.c` | Dereferencing unmapped user pointers from Ring 0 caused unrecoverable kernel panic. | Integrated `.fixup_table` exception handling returning `-EFAULT` cleanly on bad addresses. | `PASS` |
| **5** | **Kernel `panic()` on Malformed AI Input**<br>`A10:2025 - Mishandling of Exceptional Conditions` | `Nexora_Phase_14/src/ai/tensor.c`, `work.c` | Unsanitized tensor shapes or null pointers halted the operating system via `panic()`. | Replaced panics with structured error codes (`NEXORA_STATUS_INVALID_ARGUMENT`). | `PASS` |
| **6** | **Non-IRQ-Safe Trace Ring Spinlock**<br>`A06:2025 - Insecure Design` | `Nexora_Phase_17/src/phase17/trace.c` | Writer spinlock acquisition without interrupt masking risked permanent deadlock under ISR re-entry. | Enclosed spinlocks in `nx_irq_save_disable()` / `nx_irq_restore()` blocks. | `PASS` |
| **7** | **Unrestricted Capability Delegation**<br>`A01:2025 - Broken Access Control` | `Nexora_Phase_05/kernel/syscall.c` | Arbitrary processes could inject handles directly into unrelated processes' handle tables. | Enforced parent-child relationship check (`parent_pid == caller->pid`) or verified IPC channel. | `PASS` |

---

# 13. Kernel Unification Milestones (M1–M6)

* **M1 — Physical Frame Allocator:** 4 KiB page frame allocator replacing the bump allocator; verified over 10,000 churn cycles.
* **M2 — Kernel Heap with Reclamation:** Dynamic slab caches for `ai_tensor` and `ai_work_node` maintaining bounded memory watermarks.
* **M3 — Privilege Boundary Wiring:** Early-boot GDT, TSS, IDT, and per-CPU `%gs` initialization prior to kernel idle.
* **M4 — Ring 3 ELF Entry Point:** Complete user-space bootstrap transitioning from Ring 0 to Ring 3 via `sysretq` / `iretq` with working round-trip syscalls.
* **M5 — Recoverable Exception Engine:** Double-fault, general protection (#GP), and page-fault (#PF) handlers hooked into `.fixup_table`.
* **M6 — Async Work Scheduler:** Queue-driven asynchronous execution model for AI work graphs.

---

# 14. Phase Evolution Index

| Phase Directory | Focus | Subsystems & Highlights |
|-----------------|-------|-------------------------|
| `Nexora_Phase_01` – `04` | **Foundations** | Multiboot headers, serial logging, early page tables, 64-bit long-mode setup. |
| `Nexora_Phase_05` | **Privilege & Userspace** | GDT/TSS, `syscall_entry.S`, ELF64 loader, process table, capability delegation, `uaccess`. |
| `Nexora_Phase_06` – `10` | **IPC & Probing** | Synchronous IPC channels, device probes, memory map validation, test harnesses. |
| `Nexora_Phase_11` – `13` | **Security & Observability** | Security model definitions, threat model matrices, reliability metrics, policy validation. |
| `Nexora_Phase_14` | **AI Microkernel Core** | Unified tensor engine, DAG work graph scheduler, capability access control, consolidated `kmain.c`. |
| `Nexora_Phase_15` – `16` | **Validation & Hardening** | Integration test fixtures, deterministic fault injection, hardware performance qualification. |
| `Nexora_Phase_17` | **Telemetry & Release Gating** | Lockless ring-buffer tracing, automated release gating, health watchdog diagnostics. |
| `docs/` | **Interactive Documentation** | GitHub Pages web portal, interactive security findings inspector, and boot simulation suite. |

---

# 15. Interactive Documentation & Simulator

Explore the interactive web portal hosted live on GitHub Pages:

### 🔗 [https://abusuraihsakhri.github.io/Nexora/](https://abusuraihsakhri.github.io/Nexora/)

* **Architecture Overview (`index.html`):** Interactive visualization of the 8 core primitives, dual planes, phase roadmap, and boot execution.
* **Security Findings Inspector (`findings.html`):** Side-by-side interactive before/after code diffs for all 7 OWASP remediations.
* **Boot State-Machine Simulator (`boot-sim.html`):** Real-time interactive simulation modeling CPU ring transitions, GDT/IDT installation, ELF loading, and syscall round-trips.

---

# 16. Building & Testing

```bash
# Build and run unified Phase 14 AI microkernel tests
cd Nexora_Phase_14
make test

# Run Phase 05 privilege & syscall isolation tests
cd ../Nexora_Phase_05
make test

# Run Phase 17 telemetry & IRQ spinlock tests
cd ../Nexora_Phase_17
make test
```

---

# 17. Maintainer & Author

* **abusuraihsakhri**  
  * GitHub: [@abusuraihsakhri](https://github.com/abusuraihsakhri)  
  * Email: [abusuraihsakhri@gmail.com](mailto:abusuraihsakhri@gmail.com)
