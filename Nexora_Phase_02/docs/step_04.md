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
