# Step 6 — Kernel Heap

## Objective

Provide arbitrary-size dynamic kernel allocation above PMM/VMM.

## Two layers

```text
PMM
 ↓
VMM
 ↓
kernel virtual-page allocator
 ↓
kernel object allocator
 ↓
Rust alloc / Box / Vec / String
```

Do not couple every heap operation directly to page tables.

## Heap virtual region

Reserve a large high-half virtual range but map only an initial portion.

Suggested starting values:

```text
initial mapped heap: 16 MiB
growth chunk:        2 MiB
```

Exact virtual addresses remain centralized in the memory-layout module.

## Initial allocation algorithm

Use an address-sorted coalescing free-list allocator.

Features:

- arbitrary size;
- alignment;
- splitting;
- free;
- adjacent-block coalescing;
- debug metadata.

Do not begin with a complex slab/SLUB/buddy user-object allocator.

## Debug metadata

An allocation header may track:

```text
block start
block size
requested size
magic value
```

Debug builds should detect invalid and double frees.

## Heap growth

When no free block satisfies a request:

1. round growth to pages;
2. request PMM frames;
3. map VMM pages RW+NX;
4. add region to free list;
5. coalesce;
6. retry.

Do not implement heap shrinking in Phase 1.

## Global allocator

After raw allocator tests pass, integrate with Rust `GlobalAlloc` and `extern crate alloc`.

## Interrupt rule

The general heap is not interrupt-safe in Phase 1. Interrupt handlers must not allocate.

## Acceptance tests

- raw allocation;
- alignment;
- split;
- free/reuse;
- coalescing;
- heap growth;
- integrity validator;
- Box/Vec/String;
- randomized stress;
- no PMM/VMM invariant violation.
