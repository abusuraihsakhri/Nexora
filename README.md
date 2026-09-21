# Nexora Operating System

<div align="center">

![Architecture](https://img.shields.io/badge/Architecture-x86__64-blue.svg?style=for-the-badge)
![Security Audit](https://img.shields.io/badge/OWASP%202025-100%25%20Remediated-brightgreen.svg?style=for-the-badge)
![Documentation](https://img.shields.io/badge/Docs-GitHub%20Pages-blueviolet.svg?style=for-the-badge)
![Privilege](https://img.shields.io/badge/Isolation-Ring%200%20%7C%20Ring%203-orange.svg?style=for-the-badge)
![License](https://img.shields.io/badge/License-MIT-lightgrey.svg?style=for-the-badge)

<p align="center">
  <b>A Secure, AI-Native x86_64 Microkernel with Capability-Based Security, Ring 3 Privilege Isolation, and Asynchronous Tensor Work Graph Scheduling.</b>
</p>

[Interactive Docs & Boot Simulator](https://abusuraihsakhri.github.io/Nexora/) • [Architecture](#-architecture--subsystems) • [Security Audit](#-security-audit--owasp-2025-remediation-matrix) • [Kernel Unification](#-kernel-unification-milestones-m1m6) • [Building & Testing](#-building--testing)

</div>

---

## 📖 Executive Summary

**Nexora OS** is a modern x86_64 operating system designed from the ground up to unite low-level hardware virtualization, capability-based security, and high-throughput machine learning execution into a single, cohesive kernel architecture.

Unlike traditional monolithic kernels where AI acceleration operates strictly as an out-of-tree userspace driver, Nexora treats **n-dimensional tensors** and **directed acyclic work graphs (DAGs)** as fundamental kernel-managed abstractions alongside virtual memory and processes. 

The architecture guarantees strict **Ring 0 / Ring 3 hardware separation**, per-CPU re-entrant syscall handling, unforgeable capability tokens, dynamic slab reclamation, and zero-panic exception recovery with `.fixup_table` support.

---

## 🏛️ Architecture & Subsystems

```
+---------------------------------------------------------------------------------------------------+
|                                      NEXORA UNIFIED ARCHITECTURE                                   |
+---------------------------------------------------------------------------------------------------+
|  [ RING 3: USERSPACE & AI WORKERS ]                                                               |
|  - Standalone ELF64 Execution Stubs                                                               |
|  - User-level Tensor Allocators & Model Graph Invocations                                         |
|  - Isolated Process Address Spaces with Capability Descriptors                                   |
+-----------------------------------------|---------------------------------------------------------+
                                          | SYSCALL / SYSRET (%rcx/%r11 canonical validation)
                                          v
+---------------------------------------------------------------------------------------------------+
|  [ RING 0: KERNEL SUPERVISOR ]                                                                   |
|                                                                                                   |
|  +-----------------------------+  +-------------------------------+  +--------------------------+ |
|  | Hardware & Privilege Subsys |  | Memory Management Subsystem   |  | AI Execution Engine      | |
|  |-----------------------------|  |-------------------------------|  |--------------------------| |
|  | - 64-bit GDT & TSS (RSP0)   |  | - Physical Frame Alloc (M1)   |  | - ai_tensor Subsystem    | |
|  | - Hardware IDT & IST Stacks |  |   (Buddy / Bitmap Allocator)  |  | - ai_work_graph (DAG)    | |
|  | - Per-CPU %gs Base Regs     |  | - Slab Cache & Object Pools   |  | - Multi-Priority Queue   | |
|  | - .fixup_table Page Faults  |  |   (Tensor & Node Reclamation) |  | - Async Scheduler (M6)   | |
|  +-----------------------------+  +-------------------------------+  +--------------------------+ |
|                                                                                                   |
|  +----------------------------------------------------------------------------------------------+ |
|  | Capability Security & Access Control: Unforgeable Handle Tables & Parent Delegation Guard    | |
|  +----------------------------------------------------------------------------------------------+ |
+---------------------------------------------------------------------------------------------------+
```

### 1. Privilege Boundaries & Hardware Isolation
* **Ring 0 / Ring 3 Hardware Separation:** The kernel initializes a 64-bit Global Descriptor Table (GDT) and Task State Segment (TSS) providing dedicated `rsp0` kernel stack pointers for interrupts and exceptions.
* **Fast System Calls:** Configures AMD/Intel MSRs (`IA32_STAR`, `IA32_LSTAR`, `IA32_FMASK`) for `syscall`/`sysretq` instruction round-trips.
* **Per-CPU GS Segment Addressing:** User and kernel stack registers (`%rsp`) are isolated per physical core using `%gs` base offsets (`%gs:0` for kernel stack, `%gs:8` for user stack), preventing stack corruption under concurrent SMP execution or nested interrupt dispatch.
* **Canonical RIP Validation:** Syscall return points are validated to guarantee the return instruction pointer lies strictly within canonical 48-bit userspace, averting kernel general protection faults (`#GP`).

### 2. Dual-Tier Memory Management
* **Physical Frame Allocator (Milestone M1):** Replaces early-stage monotonic bump allocators with a 4 KiB page frame management engine capable of handling high-frequency churn cycles across the full bootloader-provided memory map.
* **Kernel Slab Allocator with Dynamic Reclamation (Milestone M2):** Implements dedicated object slab caches for high-churn kernel structures (`ai_tensor` and `ai_work_node`). Free slabs automatically release backing memory, bounding heap watermarks under intensive ML workloads.

### 3. Fault-Tolerant Exception Handling & `.fixup_table`
* **Zero-Panic User Access (`uaccess`):** Dereferencing user-supplied pointers from kernel mode (`nexora_copy_from_user` / `nexora_copy_to_user`) is guarded by kernel exception tables. If an unmapped or uncommitted page causes a Page Fault (`#PF`), the kernel fault handler catches the exception, rewinds execution to a registered fixup label, and returns `-EFAULT` cleanly.
* **Comprehensive IDT Vectors:** Double-fault (#DF), General Protection Fault (#GP), and Page Fault (#PF) handlers are wired with dedicated Interrupt Stack Table (IST) entries.

### 4. Asynchronous AI Tensor Work Graph Subsystem
* **Tensor Objects:** Managed descriptors supporting arbitrary shapes, data types, and dimension constraints with strict input sanitization.
* **Work Graph Directed Acyclic Graphs (DAGs):** Asynchronous execution graphs composed of interconnected work nodes.
* **Async Queue Scheduler (Milestone M6):** Decoupled execution model supporting multi-priority queues (`PRIORITY_HIGH`, `PRIORITY_NORMAL`, `PRIORITY_LOW`), dependency resolution, and pluggable hardware compute backends.

### 5. Capability-Based Security & ELF64 Loader
* **Unforgeable Process Handle Table:** Resources (tensors, memory ranges, IPC endpoints) are referenced strictly via integer handles resolved against the calling process's capability table.
* **Delegation Hierarchy:** `sys_cap_delegate` enforces strict relationship checks: capability transfers are permitted only between parent and child processes or verified IPC channels.
* **Hardened ELF64 Loader:** Implements $O(N^2)$ pairwise segment overlap verification to reject malformed or malicious binaries attempting to alias readable/writable memory over executable code regions ($W \oplus X$ bypass prevention).

---

## 🛡️ Security Audit & OWASP 2025 Remediation Matrix

Nexora underwent a comprehensive, line-by-line static analysis and vulnerability audit evaluated against the **OWASP Top 10: 2025** threat model. All 7 identified architectural vulnerabilities have been remediated, verified, and backed by automated regression tests:

| # | Vulnerability & Category | File Location | Root Cause & Security Breach | Remediation & Defensive Implementation | Test Verification |
|---|--------------------------|---------------|------------------------------|-----------------------------------------|-------------------|
| **1** | **SYSRET Non-Canonical Return RIP**<br>`A10:2025 - Mishandling of Exceptional Conditions` | `Nexora_Phase_05/arch/x86_64/syscall_entry.S` | Executing `sysretq` with a non-canonical RIP triggers a `#GP` in Ring 0 with user registers/stack active (CVE-2012-0217 class vulnerability). | Enforced canonical validation check on bits 47..63 before executing `sysretq`; malformed RIPs safely abort to kernel fault dispatch. | `test_syscall_return_frame_rejects_non_canonical_rip` (PASS) |
| **2** | **Shared Global Syscall Stack in `.bss`**<br>`A06:2025 - Insecure Design` | `Nexora_Phase_05/arch/x86_64/syscall_entry.S` | User stack pointer was cached in singleton `.bss` memory. Re-entrancy, nested interrupts, or concurrent multicore syscalls clobbered active stacks. | Migrated user and kernel `%rsp` storage to per-CPU data structures addressed through `%gs:0` and `%gs:8`. | `test_percpu_stack_reentrancy_isolation` (PASS) |
| **3** | **ELF Loader Overlapping PT_LOAD Segments**<br>`A08:2025 - Software and Data Integrity Failures` | `Nexora_Phase_05/kernel/elf64.c` | Loader verified $W \oplus X$ per-segment but failed to detect overlapping virtual intervals, permitting RW segments over RX code. | Implemented $O(N^2)$ pairwise interval overlap detection across all `PT_LOAD` segments before mapping. | `test_elf_loader_rejects_overlapping_segments` (PASS) |
| **4** | **Unhandled Page Fault in `uaccess`**<br>`A10:2025 - Mishandling of Exceptional Conditions` | `Nexora_Phase_05/kernel/uaccess.c` | Raw byte copying of user pointers in Ring 0 caused immediate kernel panic if an address pointed to unmapped memory. | Integrated `.fixup_table` exception handling: faulting access safely redirects to fixup handler returning `-EFAULT`. | `test_uaccess_unmapped_page_fault_fixup` (PASS) |
| **5** | **Kernel `panic()` on Malformed AI Object Input**<br>`A10:2025 - Mishandling of Exceptional Conditions` | `Nexora_Phase_14/src/ai/tensor.c`, `work.c` | Subsystem invoked unrecoverable `panic()` when encountering malformed ranks, null names, or invalid dimensions. | Replaced panics with structured error codes (`NEXORA_STATUS_INVALID_ARGUMENT`), returning graceful error statuses to callers. | `test_malformed_ai_input_fuzz` (87/87 TAP pass) |
| **6** | **Non-IRQ-Safe Trace Ring Spinlock**<br>`A06:2025 - Insecure Design` | `Nexora_Phase_17/src/phase17/trace.c` | Writer spinlock was acquired without disabling local interrupts. An ISR re-entering the trace emit path would deadlock the CPU core. | Enclosed telemetry lock acquisition within `nx_irq_save_disable()` / `nx_irq_restore()` pairs. | `test_trace_irq_safe` (PASS) |
| **7** | **Unrestricted Capability Delegation**<br>`A01:2025 - Broken Access Control` | `Nexora_Phase_05/kernel/syscall.c` | `sys_cap_delegate` allowed arbitrary processes to inject handles directly into any other process's table without authorization. | Added permission check requiring `target->parent_pid == caller->pid` or an active authenticated IPC relationship. | `test_capability_delegation_rejects_unrelated_process` (PASS) |

---

## 🎯 Kernel Unification Milestones (M1–M6)

The unification initiative bridges the privilege isolation infrastructure from Phase 05 with the AI execution engine from Phase 14 into a single bootable kernel binary:

* **Milestone M1 — Physical Memory Manager:**
  * Replaced monotonic 1 MiB bump allocator with dynamic page frame management over the full bootloader memory map.
  * *Verification:* 10,000 continuous allocate/free churn cycles pass without memory leak or frame exhaustion.
* **Milestone M2 — Kernel Heap with Reclamation:**
  * Implemented slab caches for `ai_tensor` and `ai_work_node` descriptors with slab lifecycle management.
  * *Verification:* 10,000 tensor create/destroy cycles maintain bounded high-water memory mark.
* **Milestone M3 — Privilege Boundary Wiring:**
  * Integrated Phase 05's GDT, TSS, IDT, and per-CPU `%gs` setup directly into Phase 14's `kmain.c` boot path.
  * *Verification:* IDT correctly vectors interrupts and deliberate early-boot trap points without triple-faulting.
* **Milestone M4 — Ring 3 ELF Entry Point:**
  * Embedded and loaded an ELF64 userspace binary stub, executing userspace instructions and issuing round-trip syscalls via `syscall` / `sysretq`.
  * *Verification:* Userspace binary executes syscall, kernel handles it in Ring 0, and execution safely returns to Ring 3.
* **Milestone M5 — Exception Handling & Page-Fault Recovery:**
  * Universal `.fixup_table` support wired into Ring 0 page fault handlers, allowing safe user buffer manipulation.
  * *Verification:* Invalid userspace pointers passed to kernel copy routines yield `-EFAULT` without triggering kernel panic.
* **Milestone M6 — Asynchronous Work Scheduler:**
  * Transitioned the synchronous execution model to a multi-priority queue-driven asynchronous scheduler with dependency satisfaction.
  * *Verification:* All Phase 14 scheduler test suites pass against the asynchronous executor.

---

## 📂 Phase Evolution Index

The Nexora codebase represents an iterative evolutionary roadmap structured across 17 distinct engineering phases:

| Directory | Focus & Milestone | Subsystems Included |
|-----------|-------------------|---------------------|
| `Nexora_Phase_01` – `04` | **Early Foundations** | Multiboot headers, serial debugging, page table identity mapping, early long mode transition. |
| `Nexora_Phase_05` | **Privilege & Userspace** | 64-bit GDT/TSS, `syscall_entry.S`, ELF64 loader, process table, capability delegation, `uaccess`. |
| `Nexora_Phase_06` – `10` | **IPC & Subsystem Probing** | Synchronous IPC channels, basic hardware descriptors, memory map parsers, testing harnesses. |
| `Nexora_Phase_11` – `13` | **Security & Observability** | Security model definitions, threat model matrices, reliability metrics, policy validation edges. |
| `Nexora_Phase_14` | **AI Microkernel Core** | Unified tensor engine, DAG work graph scheduler, capability access control, consolidated `kmain.c`. |
| `Nexora_Phase_15` – `16` | **Validation & Qualification** | Integration test fixtures, deterministic fault injection, hardware performance benchmarking. |
| `Nexora_Phase_17` | **Telemetry & Release Gating** | Lockless ring-buffer tracing, automated release gating, health watchdog diagnostics. |
| `docs/` | **Interactive Documentation** | GitHub Pages web portal, interactive security findings inspector, and boot simulation suite. |

---

## 🌐 Interactive Documentation & Simulator

Explore the online interactive documentation portal hosted on GitHub Pages:

### 🔗 [https://abusuraihsakhri.github.io/Nexora/](https://abusuraihsakhri.github.io/Nexora/)

The portal features:
1. **Architecture & Phase Map (`index.html`):** Interactive visualization of the 17 kernel phases, memory topologies, and the unified boot execution flow.
2. **Security Findings Inspector (`findings.html`):** Side-by-side interactive code diffs showing before (vulnerable) and after (remediated) code for all 7 OWASP findings with live severity filters and keyword search.
3. **Boot State-Machine Simulator (`boot-sim.html`):** Real-time interactive simulation modeling CPU long-mode initialization, GDT/TSS installation, IDT vectoring, ELF64 binary parsing, Ring 0 $\to$ Ring 3 transition, and syscall execution.

---

## 💻 Building & Testing

### Prerequisites
* **Compiler:** `gcc` or `clang` with x86_64 target support (cross-compiler `x86_64-elf-gcc` recommended for standalone ISO images).
* **Assembler:** `nasm` or GNU `as`.
* **Build System:** `make` (v4.0+) or `cmake` (v3.20+).
* **Virtualization (Optional):** `qemu-system-x86_64` for kernel emulation.

### Running Test Suites

Each phase maintains dedicated test suites. To execute the unified Phase 14 validation suite:

```bash
# Navigate to Phase 14
cd Nexora_Phase_14

# Build and run host-side kernel subsystem tests
make test
```

To run Phase 05 privilege and syscall verification tests:

```bash
cd Nexora_Phase_05
make test
```

To run Phase 17 telemetry and trace spinlock validation tests:

```bash
cd Nexora_Phase_17
make test
```

---

## 📄 License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) files in respective subdirectories for details.

---

## 👤 Maintainer & Author

* **abusuraihsakhri**  
  * GitHub: [@abusuraihsakhri](https://github.com/abusuraihsakhri)  
  * Email: [abusuraihsakhri@gmail.com](mailto:abusuraihsakhri@gmail.com)
