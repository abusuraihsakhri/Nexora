# Nexora Kernel — Phase 1 Foundation

**Project:** Nexora  
**Target:** x86-64  
**Phase:** 1 — kernel foundation  
**Status of this archive:** architecture/design package + starter source skeleton

This ZIP consolidates the Phase 1 plan into ten implementation milestones:

1. Bootstrap / kernel entry
2. Early kernel infrastructure
3. x86-64 CPU architecture foundation
4. Physical memory manager
5. Virtual memory + page-table manager
6. Kernel heap
7. Exceptions + IDT + Local APIC + monotonic timer
8. Preemptive scheduler + kernel threads
9. Ring 3 + processes + syscalls + first userspace program
10. Integration + hardening + automated testing

## Important

This archive is a **starter implementation skeleton and engineering specification**. It is intentionally not presented as a finished, bootable operating system. Several low-level pieces need to be connected to a concrete boot protocol/toolchain and completed/tested in QEMU and on hardware.

The implementation order is intentional. Do not skip memory, interrupt, or isolation invariants to reach accelerator support faster.

## Phase 1 goal

At the end of Phase 1, Nexora should be capable of:

- booting on x86-64;
- discovering and managing physical RAM;
- creating page mappings and independent process address spaces;
- allocating dynamic kernel memory;
- handling CPU exceptions;
- configuring a Local APIC timer;
- running preemptive kernel threads;
- entering Ring 3;
- servicing a minimal syscall ABI;
- isolating processes;
- surviving userspace faults;
- running automated self-tests.

AI-specific differentiation belongs after this foundation is stable.

See `docs/phase1/` for the complete milestone specifications.
