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
