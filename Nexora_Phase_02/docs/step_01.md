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
