# Step 6 — `VmSpace` Address-Space Abstraction

## Objective
Make `VmSpace`, not page tables, the public representation of an address space.

## `VmSpace` responsibilities
Architecture root, high-level lock, region tree, refcount, active CPU mask, TLB generation, user limits, lifecycle, accounting, and future memory policy.

## Kernel/user spaces
Adopt the Step-5 kernel root into permanent `nexora_kernel_space`. User-space creation allocates a new user root and inherits/shares canonical kernel top-level entries.

## Ownership
User page-table structures are owned by the space. Kernel subtrees are shared. Destruction frees only owned user structures plus the per-space root.

## Lifetime
Reference-count the space because many threads may share it. Lifecycle states: ALIVE → DYING → DEAD. Final destruction waits until the space is no longer active.

## Locking
Use one high-level `VmSpace` lock initially. Public APIs lock; compound operations use internal `_locked` variants. Document lock order.

## Accounting
Track virtual/resident bytes, region count, 4K/2M/1G mappings, page-table bytes/frames, and a monotonic `vm_space_id_t` for diagnostics.

## Clone model
Distinguish empty creation, deep copy, and COW clone. COW is added after regions/faults/refcounts are available.

## AI policy hook
Reserve per-space defaults for NUMA, huge pages, HBM/DRAM placement, pin limits, and device affinity; regions may override them.
