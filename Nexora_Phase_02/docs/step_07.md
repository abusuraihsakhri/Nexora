# Step 7 — Virtual Memory Regions / VMA Manager

## Objective
Represent logical virtual memory independently of current physical residency.

## `VmRegion`
Stores half-open VA range, permissions, region flags, memory intent, backing object/offset, policy, and intrusive balanced-tree links.

## Tree
Use a red-black tree keyed by start. Regions never overlap. Point lookup/insert/remove/predecessor/successor are O(log n). Automatic gap search can begin as O(n) and later gain largest-gap augmentation.

## Region semantics
Flags include anonymous/shared/private, grow-down/up, guard, COW, lazy, device, pinned, huge-prefer/require.

## Reservation vs population
Creating a lazy region reserves VA and increases virtual accounting without allocating physical memory.

## Placement
Support fixed placement and automatic placement with size, alignment, min/max, direction, and future ASLR constraints.

## Split/merge/remove/protect
Partial unmap/protect operations split regions as necessary. Backing offsets must advance correctly after splits. Adjacent regions merge only if semantics, policy, backing object, and offsets match.

## Authoritative metadata
`VmRegion.permissions` is the logical truth. Resident PTEs are only the current hardware realization.

## Guard/stack
Guard regions exist logically but remain unmapped. Grow-down stacks require explicit bounds/guard rules.

## VmObject
Reserve backing-object references for Anonymous, File, Shared, Device, Tensor, Physical, Zero, etc.

## Transactionality
Preallocate metadata needed for split operations before mutating the live tree.

## Required tests
Insertion, overlap rejection, adjacency, boundary lookup, split/merge, partial removal/protection, lazy accounting, automatic hole allocation, guard behavior, randomized tree stress.
