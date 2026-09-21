# Nexora Operating System

> **Nexora OS** is a secure, AI-native x86_64 microkernel architecture engineered with capability-based access control, strict Ring 3 privilege isolation, and asynchronous tensor work graph scheduling.

---

## 🌟 Architecture Overview

Nexora bridges low-level hardware virtualization and memory safety with high-throughput AI workloads:

* **Privilege Separation & Isolation:** Strict separation between Ring 0 (Kernel Supervisor) and Ring 3 (User / AI Workers) with hardware-enforced TSS/GDT and per-CPU `%gs` syscall handling.
* **Kernel Memory Subsystem:** Physical frame allocator paired with slab caches supporting automatic reclamation for high-churn AI tensors and asynchronous work nodes.
* **Fault-Tolerant Exception Handling:** Hardware IDT with `.fixup_table` support for zero-panic user memory copy operations (`uaccess`).
* **Asynchronous Tensor Scheduling:** Multi-priority work graph executor allowing decoupled dispatch across execution backends.
* **Fine-Grained Capability Security:** Unforgeable handle table restricting inter-process communication and hardware manipulation.

---

## 🛡️ Security Audit & Hardening (OWASP 2025 Compliant)

All 7 core architectural vulnerabilities identified during security auditing have been remediated, verified, and backed by regression tests:

| # | Vulnerability & Category | File Location | Resolution Summary |
|---|--------------------------|---------------|-------------------|
| **1** | **SYSRET Non-Canonical Return RIP**<br>`A10:2025 - Mishandling of Exceptional Conditions` | `Nexora_Phase_05/arch/x86_64/syscall_entry.S` | Enforced canonical address validation on return `%rcx` before `sysretq`, redirecting malformed user RIPs to safe fault dispatch. |
| **2** | **Shared Global Syscall Stack in .bss**<br>`A06:2025 - Insecure Design` | `Nexora_Phase_05/arch/x86_64/syscall_entry.S` | Migrated user and kernel `%rsp` scratch slots to per-CPU storage addressed via `%gs` to eliminate re-entrancy and SMP race conditions. |
| **3** | **Overlapping PT_LOAD Segments ($W \oplus X$ Bypass)**<br>`A08:2025 - Software and Data Integrity Failures` | `Nexora_Phase_05/kernel/elf64.c` | Implemented $O(N^2)$ interval overlap rejection in ELF64 loader to prevent malicious write/execute memory segment aliasing. |
| **4** | **Unhandled Page Fault in `uaccess`**<br>`A10:2025 - Mishandling of Exceptional Conditions` | `Nexora_Phase_05/kernel/uaccess.c` | Bound kernel-mode user buffer access to `.fixup_table` handler, converting invalid user pointers into recoverable `-EFAULT` returns. |
| **5** | **Panic on Malformed AI Object Input**<br>`A10:2025 - Mishandling of Exceptional Conditions` | `Nexora_Phase_14/src/ai/tensor.c`, `work.c` | Replaced unrecoverable `panic()` invocations with structured status codes (`NEXORA_STATUS_INVALID_ARGUMENT`). |
| **6** | **Non-IRQ-Safe Trace Ring Spinlock**<br>`A06:2025 - Insecure Design` | `Nexora_Phase_17/src/phase17/trace.c` | Wrapped telemetry buffer acquisition in `irq_save_disable()` / `irq_restore()` pairs to prevent interrupt-reentrancy deadlocks. |
| **7** | **Unrestricted Capability Delegation**<br>`A01:2025 - Broken Access Control` | `Nexora_Phase_05/kernel/syscall.c` | Restricted `sys_cap_delegate` to verified parent-child process relationships and authenticated IPC channels. |

---

## 🚀 Kernel Unification Milestones (M1–M6)

The unified kernel integrates Phase 05's privilege infrastructure with Phase 14's AI engine:

* **M1 — Physical Frame Allocator:** Replaced monotonic bump allocator with dynamic page frame management across the bootloader memory map.
* **M2 — Slab Allocator with Reclamation:** Dynamic slab caches for `ai_tensor` and `ai_work_node` with bounded memory watermarks under cyclic churn.
* **M3 — Privilege Boundary Wiring:** Early-boot initialization of GDT, TSS, IDT, and per-CPU `%gs` base register prior to system idling.
* **M4 — Ring 3 ELF Entry Point:** Complete user-space bootstrap transitioning from Ring 0 to Ring 3 via `sysretq` / `iretq` with working round-trip syscalls.
* **M5 — Recoverable Exception Engine:** Double-fault, general protection (#GP), and page-fault (#PF) handlers hooked into fixup tables.
* **M6 — Async Work Scheduler:** Queue-driven asynchronous execution model for AI work graphs.

---

## 📊 Interactive Documentation & Simulator

An interactive findings portal and boot simulation suite is available in the [`docs/`](./docs) directory:

* **[`docs/index.html`](./docs/index.html):** Architecture diagrams, phase map, and unification roadmap.
* **[`docs/findings.html`](./docs/findings.html):** Interactive vulnerability inspector featuring side-by-side before/after diffs sourced dynamically from [`docs/findings.json`](./docs/findings.json).
* **[`docs/boot-sim.html`](./docs/boot-sim.html):** Visual state-machine simulator modeling CPU ring transitions, GDT/IDT installation, ELF loading, and syscall round-trips.

---

## 🛠️ Repository Structure

```text
Nexora/
├── docs/                     # Interactive GitHub Pages documentation & simulator
├── Nexora_Phase_01/ - 04/    # Early architectural foundations & boot probes
├── Nexora_Phase_05/          # Privilege isolation, GDT/IDT, syscall, ELF loader
├── Nexora_Phase_06/ - 13/    # IPC, device interfaces, capability tables, memory
├── Nexora_Phase_14/          # Unified AI microkernel, tensor engine, scheduler
├── Nexora_Phase_15/ - 16/    # Integration fixtures and hardware validation
├── Nexora_Phase_17/          # Tracing, telemetry, and release-gate verification
└── NEXORA_REMEDIATION_PLAN.md # Execution & remediation tracking manifest
```

---

## 👤 Author

* **abusuraihsakhri** ([abusuraihsakhri@gmail.com](mailto:abusuraihsakhri@gmail.com))
