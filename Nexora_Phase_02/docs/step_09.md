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
