# Nexora — Phase 2: Virtual Memory & Address Spaces

This archive consolidates the complete Phase 2 design developed in this chat.

## Scope

1. Paging primitives and virtual-address model
2. x86-64 page-table manager
3. Mapping / unmapping / protection API
4. TLB management and address-space activation
5. Canonical kernel virtual-memory layout
6. `VmSpace` address-space abstraction
7. Virtual-memory regions / VMA manager
8. Page-fault subsystem and demand paging
9. Frame reference counting, copy-on-write, huge pages, and VM hardening
10. Integration, stress testing, and Phase-2 acceptance

## Architectural boundary

```text
Boot memory map
      ↓
Physical Memory Manager
      ↓
Frame database / refcounts
      ↓
VmSpace
      ↓
VmRegion tree / VmObject
      ↓
Fault resolver / COW / huge-page policy
      ↓
VM mapper
      ↓
TLB layer
      ↓
x86-64 page tables
```

## Archive contents

- `docs/step_01.md` … `docs/step_10.md`: Phase 2 step specifications.
- `PHASE_2_ALL_IN_ONE.md`: consolidated specification.
- `API_SUMMARY.md`: intended public VM API boundary.
- `X86_64_LAYOUT.md`: proposed kernel virtual layout.
- `source_scaffold/`: interface/source-tree scaffold matching the design.

The source scaffold is deliberately marked as scaffold rather than a hardware-validated implementation; it must be connected to the actual Phase 1 PMM, boot, allocator, and interrupt code.
