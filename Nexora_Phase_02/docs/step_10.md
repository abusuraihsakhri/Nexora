# Step 10 — VM Integration, Stress Testing & Phase-2 Completion

## Objective
Freeze the public VM boundary, integrate all subsystems, and prove correctness under faults, stress, and repeated lifecycle operations.

## Stable kernel-facing API
Other Nexora subsystems use `VmSpace`, `VmRegion`, mapping/protection/query APIs, fault/object APIs, COW clone, and diagnostics. Raw PML4/PTE/CR3/INVLPG stay architecture-private.

## Boot order
Early console → boot info → memory map → bootstrap PMM → frame database → architecture paging → Nexora kernel root/direct map → CR3 switch → full PMM → kernel `VmSpace` → regions → #PF → heap/metadata allocator → VM self-tests.

## Test profiles
FAST boot sanity; FULL emulator/CI; STRESS deterministic fuzzing and failure injection.

## Integration tests
Exercise PMM allocation, map, real CPU read/write, direct-map alias verification, protect, TLB enforcement, unmap, release.

## Demand/COW/huge tests
Large lazy reservations must allocate only touched pages. COW stress validates contents/refcounts. Huge-page round-trip 512×4K → 2M → 512×4K preserves semantics.

## Fault injection
Deterministically fail PMM, page-table, VMA metadata, COW, and huge-split allocations. Every recoverable failure must leave the prior address-space state valid and leak-free.

## VM fuzzer
Randomly create/remove/protect/map/unmap/fault/clone/promote/split using a reproducible seed. Validate the region tree, page tables, PMM, and refcounts after every operation; optionally compare with a simple shadow model.

## Boundary tests
Exercise 4K/2M/1G boundaries, canonical split, user maximum, kernel base, null page, and address overflow.

## Isolation/CR3/TLB stress
Repeatedly switch spaces that map the same VA to different PAs and remap/protect the same VA; stale translations must never survive a completed VM operation.

## Final invariants
No frame reused while referenced; no stale TLB can reach a released frame; no VMA overlap; no USER kernel mapping; no normal RWX; anonymous pages zeroed before exposure; COW isolates writes; user destruction cannot destroy shared kernel mappings; huge transforms preserve contents and protection.

## Acceptance
Phase 2 is complete when Nexora boots on its own page tables, supports isolated `VmSpace`s, stable VMA operations, demand paging, COW, huge-page split/promotion, correct TLB semantics, zero lifecycle leaks, and successful W^X/NX/kernel-user/fault-injection/fuzz audits.
