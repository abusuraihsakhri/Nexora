# Nexora Phase 4 Completion Record

## Phase 4 goal

Build a protected shared-tensor mechanism allowing multiple Nexora work domains
to access the same tensor backing without duplicating payload memory.

## Delivered architecture

```text
Work Domain A                              Work Domain B
     |                                          |
opaque handle A                            opaque handle B
     |                                          |
     +--------- rights / generation ------------+
                        |
                        v
                   ai_tensor
                        |
                        v
                ai_tensor_backing
                        |
                 shared payload pages
```

The implementation now contains:

1. first-class tensor backing storage;
2. work-domain ownership and lifecycle;
3. generation-protected opaque handles;
4. explicit per-handle rights;
5. rights-attenuated SHARE and TRANSFER;
6. domain-local zero-copy mapping metadata;
7. managed reference-counted object lifetime;
8. local revocation and concurrency-safe pinned access;
9. adversarial/fuzz/race/stress validation;
10. benchmark and end-to-end integration gate.

## Security properties demonstrated by the prototype

- raw kernel pointers are not externally used as capabilities;
- copied tokens are scoped to the encoded owning domain;
- stale handles/mappings fail after generation advancement;
- generation counters do not wrap and revive ancient tokens;
- rights can be attenuated but not amplified;
- read-only tensor policy blocks WRITE authority;
- SHARE/TRANSFER require explicit delegation rights;
- transfer commits destination-first and rolls back on source revalidation failure;
- local revocation blocks future resolution/acquisition;
- established pinned references remain valid during concurrent revocation/unmap;
- final-reference retirement occurs exactly once under the tested concurrency model.

## Zero-copy property demonstrated

Two domain-local mappings can resolve to different logical virtual addresses
while carrying the same physical-backing token and the same kernel payload
address. Writes made through the producer mapping are immediately visible
through a recipient read mapping without a tensor-sized copy.

## What Phase 4 does not yet prove

Phase 4 is **not** a complete security boundary against hostile user processes.
The current kernel still lacks:

- user mode and a syscall ABI;
- per-domain hardware page tables;
- real MMU permission installation;
- IOMMU/device isolation;
- APIC/SMP kernel execution;
- IRQ-safe/preemption-aware locking;
- a reclaiming page-frame allocator/kernel heap;
- transitive capability-lineage revocation;
- synchronization semantics for overlapping payload writes.

Those limitations define the next implementation phases rather than hidden
assumptions in the current prototype.

## Phase 4 exit decision

The shared-tensor object/capability/lifetime design is sufficiently coherent to
move to **Phase 5 — user mode and syscall ABI**, where the logical isolation
model must be bound to actual address spaces and measured across a real
user/kernel boundary.
