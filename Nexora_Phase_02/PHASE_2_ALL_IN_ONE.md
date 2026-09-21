# Nexora — Phase 2 Complete Specification

# Step 1 — Paging Primitives & Virtual Address Model

## Objective
Define the types and invariants used by every later VM component before touching hardware page tables.

## Core design
- Page sizes: 4 KiB, 2 MiB, 1 GiB.
- Checked alignment helpers: `is_aligned`, `align_down`, `align_up_checked`.
- Strong types: `PhysicalAddress`, `VirtualAddress`, `PhysicalFrame`, `VirtualPage`.
- Reject noncanonical x86-64 VAs; do not truncate.
- For 48-bit paging, extract PT/PD/PDPT/PML4 indices from bits 12–47.
- Use half-open ranges `[start,end)`.
- Keep generic permissions separate from mapping semantics.

## Generic permissions
`READ`, `WRITE`, `EXECUTE`, `USER`.

## Mapping flags
`GLOBAL`, `WIRED`, `DEVICE`, `SHARED`, `COW`, `LAZY`, `GUARD`, `HUGE`.

## Security
Default W^X policy: RWX is rejected. JIT-style code transitions RW → RX.

## AI-oriented metadata
Reserve `MemoryIntent`: General, Kernel, User, Code, Stack, Heap, Tensor, ModelWeights, KVCache, DMA, AcceleratorShared, Device, Temporary.

## Architecture split
Generic VM owns semantic types and policy. x86 code owns canonical-address rules, page-table encoding, and hardware-specific details.

## Required tests
Alignment edge cases, overflow, canonical/noncanonical addresses, index extraction, aligned page/frame construction, and W^X rejection.

---

# Step 2 — x86-64 Page-Table Manager

## Objective
Build the architecture-specific four-level page-table machinery.

## Structure
`PML4 → PDPT → PD → PT → 4 KiB frame`; each table is one 4 KiB page containing 512 64-bit entries.

## Requirements
- Wrap raw PTE values and expose helpers for Present, Writable, User, Huge, Global, NX, address and flags.
- Root is stored as a physical address because CR3 refers to a physical root.
- Allocate every new page-table level from PMM and zero it before installation.
- Provide lookup-only and create-missing walking modes.
- Recognize 1 GiB/2 MiB huge leaves at PDPT/PD; never treat them as child tables.
- Preserve parent traversal permissions for user/writable mappings.
- Provide virtual→physical translation.
- Destruction frees page-table frames, not blindly the mapped data frames.
- Keep shared kernel subtrees externally owned.
- Expose CR3 read/write helpers but keep PCID-ready abstractions.
- Roll back intermediate table allocations if a walk fails.
- Provide validation and a page-table dump.

## Required tests
Empty root, missing lookup, create walk, hierarchy reuse, translation, huge-page recognition, injected OOM rollback, and safe destruction.

---

# Step 3 — Mapping, Unmapping & Protection API

## Objective
Expose generic `map`, `unmap`, `protect`, `query`, and `translate` operations over Step 2.

## Validation
Before changing page tables validate canonicality, physical-address support, alignment, page size, permissions, W^X, overflow, address-space limits, and existing mappings.

## Semantics
- Mapping over an existing leaf is rejected by default.
- Range mapping is transactional; partial success is rolled back.
- Unmapping returns old mapping metadata but does not automatically free its data frame.
- Empty PT/PD/PDPT tables can be reclaimed.
- Protection changes modify permissions without changing physical backing.
- 4 KiB leaves live at PT, 2 MiB at PD, 1 GiB at PDPT.
- Mapping a 4 KiB page inside an existing huge mapping yields a page-size conflict until splitting exists.
- Device mappings carry explicit cache/memory-type semantics.
- Guard ranges remain logically reserved with no present PTE.

## TLB contract
Page-table mutation records required invalidation; generic VM does not contain raw x86 invalidation assembly.

## Required tests
Single/adjacent maps, double-map rejection, unmap, range rollback, permission changes, RWX rejection, huge mappings/conflicts, table reclamation, device flags, query/translate.

---

# Step 4 — TLB Management & Address-Space Activation

## Objective
Make page-table changes visible to CPUs and establish safe address-space switching.

## TLB API
Generic operations: invalidate page, invalidate range, flush space, flush all. x86 implements them with `INVLPG`, CR3 reloads, and later INVPCID.

## Current-space model
Each CPU tracks its current `VmSpace`. If a modified space is inactive locally, do not perform pointless local invalidation.

## Activation
Validate root, load CR3, update CPU-local current-space state and active-CPU tracking. Avoid CR3 reloads when switching threads that already share one address space.

## Shared kernel mappings
Initial process model: private user lower half, shared kernel upper half. Kernel subtrees survive user-space destruction.

## Batching
Small ranges use per-page invalidation; large ranges use a context flush. Changes should be batched around mapping transactions.

## PCID/SMP readiness
Reserve PCID state, TLB generations, active CPU masks, and future shootdown messages. Do not enable complex PCID behavior until the baseline is correct.

## Critical invariant
Do not free/reuse a frame until every CPU that could still hold a stale translation has synchronized.

## Required tests
A→B activation, same-space no-op switch, stale-unmap protection, RW→R and X→NX enforcement, inactive-space edits, range flushing, global mapping behavior, shared kernel stability.

---

# Step 5 — Canonical Kernel Virtual-Memory Layout

## Objective
Replace bootloader-owned temporary mappings with a Nexora-owned permanent kernel address space.

## Proposed 48-bit x86-64 layout
- Direct map: `0xFFFF800000000000 .. 0xFFFFC00000000000`
- Kernel heap: `0xFFFFC00000000000 .. 0xFFFFD00000000000`
- Vmalloc: `0xFFFFD00000000000 .. 0xFFFFD80000000000`
- MMIO: `0xFFFFD80000000000 .. 0xFFFFE00000000000`
- Accelerator region: `0xFFFFE00000000000 .. 0xFFFFE80000000000`
- Temporary mappings: `0xFFFFE80000000000 .. 0xFFFFF00000000000`
- Kernel image minimum: `0xFFFFFFFF80000000`

These are x86-64 port choices, not universal Nexora constants.

## Kernel image protection
`.text` R-X; `.rodata` R--; `.data/.bss` RW-; all supervisor-only; writable data NX.

## Direct map
Map appropriate RAM using firmware/boot memory types; keep physical holes/MMIO distinct. Prefer 2 MiB pages plus 4 KiB edges initially. Explicit `dmap_phys_to_virt` helpers replace ad hoc pointer arithmetic.

## Reserved windows
Heap/vmalloc reserve VA space without mapping all of it. MMIO uses explicit `ioremap` semantics. Accelerator memory gets a dedicated future-facing region. Temporary mapping slots are reserved for short-lived physical access.

## Boot transition
The new root must map current RIP, stack, GDT, IDT, TSS/IST, page tables, CPU-local state, and early console/device mappings before CR3 is switched.

## Security/readiness
Enable NX, keep KASLR-ready runtime layout metadata, leave broad identity maps behind after boot, and audit USER bits in the kernel half.

---

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

---

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

---

# Step 8 — Page-Fault Subsystem

## Objective
Connect x86 `#PF` (vector 14) to `VmSpace`/`VmRegion` so valid missing pages can be created on demand.

## x86 input
Capture CR2, error code, and exception frame. Decode P, W/R, U/S, RSVD, and I/D. Keep decoding extensible for modern additional fault causes.

## Generic fault context
Normalize to address/page, read/write/execute, present/protection state, origin, and instruction pointer. Architecture code decodes; generic VM decides semantics.

## Classification
- no region → invalid
- GUARD → guard fault
- access not allowed → permission fault
- valid lazy nonresident anonymous page → allocate/zero/map
- logically writable COW page with read-only PTE → COW path
- RSVD → page-table corruption path

## Demand-zero
On first valid access: serialize, re-check mapping, allocate a frame, zero it, map with region permissions, update resident accounting, return resolved. Exception return retries the original instruction.

## Security
Never expose stale physical contents to a new anonymous user page. W^X remains enforced during fault resolution.

## Concurrency
Two simultaneous faults on one page must not leak or install competing frames. Coarse `VmSpace` locking is acceptable initially.

## Kernel faults
Only explicitly supported kernel demand regions are recoverable. Unknown kernel faults should emit rich diagnostics and panic instead of silently allocating.

## Recursive faults
Track per-CPU page-fault depth and route nested faults to an emergency diagnostic path.

## OOM/object hooks
Return explicit OOM results and prepare object-driven fault dispatch for file/shared/device/tensor objects.

## Required tests
First touch, repeat touch, sparse demand paging, write/execute protection, guards, invalid address, same-page concurrency, injected OOM, RSVD fatal path, virtual vs resident accounting.

---

# Step 9 — Frame Refcounts, COW, Huge Pages & VM Hardening

## Objective
Support shared physical memory safely, real copy-on-write, huge-page transformations, and stronger kernel protection.

## Frame database
Index metadata by PFN. Keep atomic refcount, classification flags, NUMA/zone placeholders, and a separate pin count. A fresh managed allocation begins with one ownership reference.

## Ownership semantics
Direct-map aliases do not create ownership refs. MMIO/device memory may be externally owned. Refcount underflow is a kernel invariant failure.

## COW clone
For private writable resident pages: increment frame reference, map child read-only, make parent read-only, mark COW, and synchronize parent TLB. Nonresident lazy pages stay absent in both. Shared and immutable mappings follow their own semantics.

## COW write fault
- refcount == 1 → no-copy fast path: remove COW, make same frame writable, invalidate TLB.
- refcount > 1 → allocate/copy/replace/invalidate/drop old ref.

## Huge pages
Promote 512 compatible contiguous 4 KiB mappings into one 2 MiB leaf. Require virtual/physical alignment, physical contiguity, uniform permissions/memory type/object policy, and compatible COW state.

Split a 2 MiB leaf by allocating one PT and materializing 512 equivalent 4 KiB entries, then synchronize TLB before fine-grained changes.

COW writes inside a shared 2 MiB mapping initially split first, then perform 4 KiB COW.

## Hardening
- strict W^X
- heap/stacks/page tables/MMIO/direct map NX
- kernel text R-X and rodata R--
- remove writable direct-map aliases to kernel text/rodata frames
- JIT transition RW → RX, not permanent RWX

## AI policy
Model weights prefer huge read-only shared mappings; tensors/KV cache can prefer 2 MiB where appropriate; sharing semantics stay separate from memory intent.

## Required tests
Frame refcounts, COW isolation and fast path, lazy COW region, immutable sharing, huge split/promotion/rejection, fine-grained protection after split, COW inside huge mapping, direct-map hardening, NX audit, clone/write/destroy stress.

---

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

---

