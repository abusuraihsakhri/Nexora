# Nexora Kernel — Remediation & Unification Plan

Status: draft, awaiting approval before implementation begins.
Scope: (1) fix the 7 findings from the security audit, (2) unify Phase 5 (Ring 3 / syscalls / ELF loader) with Phase 14 (AI kernel primitives) into one kernel image that boots to userspace, (3) publish an interactive findings site under `docs/` for GitHub Pages.

No code changes have been made yet. This file is the plan only.

## 0. Ground truth

The tree is 17 independent phase directories, each with its own `Makefile`/build, not a single buildable kernel. The only phases relevant here:

- `Nexora_Phase_05` — GDT, syscall entry (`arch/x86_64/syscall_entry.S`), ELF64 loader, process table, uaccess, handle table. Builds a Ring 3 capable image but is not wired into Phase 14.
- `Nexora_Phase_14` — consolidated AI kernel (`kmain.c`, bump allocator, tensor/work/scheduler/capability subsystems). Boots in Ring 0 only, ends in `cli; hlt`.
- `Nexora_Phase_17` — tracing/watchdog/release-gate tooling layered on top of Phase 14.

There is currently no repo-root build that links Phase 5 and Phase 14 together. That link is milestone M4 below.

## 1. Security fixes (map to audit findings)

Each item: file, change, and the verification that proves it.

| # | Finding | File(s) | Fix | Verify |
|---|---|---|---|---|
| 1 | SYSRET non-canonical RIP → #GP in Ring 0 | `Nexora_Phase_05/arch/x86_64/syscall_entry.S` | Canonical-address check on return RIP before `sysretq`; fall back to safe fault path instead of executing sysret on a bad address | Unit test: craft a syscall return frame with a non-canonical RIP, assert the kernel does not execute `sysretq` on it (harness in `Nexora_Phase_05/tests`) |
| 2 | Shared global syscall stack (`.bss` scratch, not per-CPU) | `syscall_entry.S`, need per-CPU data block | Move `nexora_syscall_user_rsp`/`kernel_rsp` into a per-CPU struct addressed via `%gs` | Since Phase 5 is currently single-CPU/no-SMP, add a regression test asserting two nested/re-entrant syscall entries don't clobber the same stack slot (simulate via IRQ-during-syscall in the test harness) |
| 3 | ELF loader doesn't reject overlapping PT_LOAD segments (W^X bypass) | `Nexora_Phase_05/kernel/elf64.c` | O(n²) pairwise interval-overlap check across PT_LOAD segments before mapping any of them | New test fixture: hand-built ELF with overlapping RX + RW segments, assert `nexora_elf64_load` returns `NEXORA_EINVAL` |
| 4 | `uaccess.c` byte copy has no page-fault fixup → kernel panic on bad user pointer | `Nexora_Phase_05/kernel/uaccess.c` | Exception table (`.fixup_table`) + fault handler returning `-EFAULT` instead of faulting in Ring 0 | Test: pass an unmapped-but-in-range user pointer to `nexora_copy_from_user`, assert `-EFAULT` return, no panic |
| 5 | `panic()` on malformed AI object input (tensor/work) | `Nexora_Phase_14/src/ai/tensor.c`, `work.c` | Replace `panic()` calls with `nexora_status_t` error returns (`ai_tensor_create_safe`, etc.); callers propagate the error | Fuzz test: malformed dims/rank/null name inputs must return an error status, not halt the process |
| 6 | Trace ring-buffer spinlock not IRQ-safe → deadlock if ISR re-enters same slot | `Nexora_Phase_17/src/phase17/trace.c` | `irq_save_disable`/`irq_restore` wrapping the spinlock critical section | Test: simulate a nested call to `nx_trace_emit` from within an emit (mock ISR reentry), assert no hang |
| 7 | `sys_cap_delegate` lets any process inject handles into any other process | `Nexora_Phase_05/kernel/syscall.c` | Require `target->parent_pid == caller->pid` or an established IPC channel before allocating the handle | Test: non-parent, non-channel process attempts delegation, assert `-EPERM` |

Sequencing: fix 4 and 1 first (they're both "kernel panics/faults on attacker-controlled input" — highest severity, smallest diff). Then 3, 6, 7, 2, 5 in that order. Each fix lands as its own commit with its own test, so a bisect can isolate a regression to one finding.

## 2. Kernel unification (Phase 5 + Phase 14 → one bootable image)

This is the larger effort. Milestones, each with an explicit exit criterion:

- **M1 — Physical memory manager.** Replace `bump.c`'s 1 MiB monotonic allocator with a buddy/bitmap frame allocator over the full memory map from the bootloader. *Verify: allocate/free churn test runs 10k cycles without exhausting frames that were freed.*
- **M2 — Kernel heap with reclamation.** Slab caches for `ai_tensor` and `ai_work_node` (the two hot allocation types) on top of M1. *Verify: create/destroy 10k tensors, confirm heap high-water mark stays bounded instead of monotonically growing.*
- **M3 — Privilege boundary wiring.** Bring Phase 5's GDT/TSS, IDT, and per-CPU GS setup into the Phase 14 boot path (`kmain.c`) so it runs before the `cli; hlt` halt. *Verify: kernel boots, installs IDT, and a deliberate `int3` in early boot is caught by a handler instead of triple-faulting.*
- **M4 — Ring 3 entry point.** After M3, load a trivial ELF64 userspace stub via Phase 5's loader and `iretq`/context-switch into it instead of halting. *Verify: a "hello syscall" userspace binary executes one syscall (e.g. `sys_yield` or a debug print) and the kernel resumes in Ring 0 to handle it.*
- **M5 — Exception handling.** Double-fault and page-fault handlers with `.fixup_table` support (needed by security fix #4 above, generalized to all kernel-mode user-memory touches, not just `uaccess.c`). *Verify: a Ring 3 process touching an unmapped page produces a recoverable fault path (SIGSEGV-equivalent) rather than a kernel panic.*
- **M6 — Work scheduler bridging.** Bridge `ai_work_graph` execution from the current synchronous READY→RUNNING→DONE simulation to an async queue model (starting with a software-only queue; real PCIe/VirtIO backend is out of scope for this plan). *Verify: existing Phase 14 scheduler tests pass unchanged against the new async queue.*

M1–M3 must land before M4 (Ring 3 needs a real heap and IDT). M5 depends on M3's IDT. M6 is independent and can happen in parallel with M4/M5.

## 3. Interactive findings site (`docs/` for GitHub Pages)

Static HTML/CSS/JS, no build step, lives at `Nexora/docs/`.

- `docs/index.html` — architecture overview: phase timeline (Phase 1→17), diagram of what's wired together today vs. after unification.
- `docs/findings.html` — the 7 audit findings, each with: severity badge, vulnerable code (before), fixed code (after), and a toggle to diff them side by side. Sourced from a `docs/findings.json` data file so the plan doc and the page stay in sync — the page renders from data, findings aren't hand-duplicated into HTML.
- `docs/boot-sim.html` — a small JS state-machine simulation of the boot path (Ring 0 → GDT/IDT setup → ELF load → Ring 3 entry → syscall round-trip) so the M1–M6 unification milestones are visible as an animated sequence rather than prose.
- `docs/status.json` — tracks which findings/milestones are done vs. planned; the pages read this so the site reflects real progress instead of going stale.

This requires the directory to become a real git repository (it currently isn't one) before GitHub Pages can serve it. That's a prerequisite step, not implied by writing the files.

## 4. Sequencing and what I need from you before I start

1. Confirm findings-first: I implement and test items 1–7 in §1 individually (one commit each), since they're small, independent, and each has a clear pass/fail test.
2. Confirm unification milestones M1–M6 proceed in the order above, one at a time, each with its own verification before moving to the next — this is a multi-day effort, not one sitting.
3. The `docs/` site (§3) can start as soon as `findings.json` has real before/after data, i.e. after §1 is done — building it earlier means hand-writing placeholder content that has to be redone.
4. This directory needs `git init` before anything can be pushed to GitHub Pages. I have not done this yet — say the word and I will, or you can run it yourself.

Once you confirm this plan (or tell me what to change), I'll start with security fix #1 (SYSRET canonical check) and work down the table in §1, then move to M1.
