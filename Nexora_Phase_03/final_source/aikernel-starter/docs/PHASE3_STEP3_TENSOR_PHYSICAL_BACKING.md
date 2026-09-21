# Phase 3 — Step 3: Tensor Physical Backing

## Goal

Step 2 deliberately separated tensor semantics from storage. Step 3 makes that
separation concrete by introducing `NX_OBJECT_MEMORY` and binding tensors to a
memory object through generation-checked kernel handles.

The invariant is now:

```text
tensor metadata != allocation

tensor --handle--> nx_memory --backend--> resident bytes
```

This is required for later tensor views, shared backing, ownership/reference
counting, placement, zero-copy IPC, and deterministic reclamation.

## Current bootstrap backend

The current kernel still uses the monotonic early heap. `nx_memory` therefore
allocates resident storage from that arena, but it does so with a page-oriented
contract:

- backing begins on at least a 4 KiB boundary;
- capacity is rounded up to 4 KiB pages;
- requested bytes and resident bytes are tracked separately;
- the memory object is registered as `NX_OBJECT_MEMORY`;
- bounds-checked address translation is provided through `nx_memory_ptr()`.

The boot code identity-maps the first 1 GiB. The early heap lives in the kernel
image inside that region, so `nx_memory.physical_base` is valid under the
current bootstrap mapping. This is **not yet a general PMM/VMM implementation**:
the early heap cannot return pages and it does not parse a firmware/Multiboot
memory map. The new object API intentionally isolates that limitation so the
backend can later be replaced by a page-frame allocator without changing tensor
handles.

## Memory object

A memory object contains:

```c
typedef struct nx_memory {
    nx_object object;
    nx_memory_backend backend;
    void *kernel_address;
    u64 physical_base;
    u64 size_bytes;
    u64 capacity_bytes;
    u64 alignment;
    u32 flags;
} nx_memory;
```

`size_bytes` is the requested logical storage span. `capacity_bytes` is the
resident allocation rounded to the current 4 KiB granule.

The initial backend is:

```text
NX_MEMORY_BACKEND_EARLY_HEAP
```

The interface is deliberately backend-neutral so later implementations can add
page-frame, device-memory, external/DMA, or remote-memory backends.

## Fallible bootstrap allocation

Step 3 adds:

```c
void *kalloc_try(usize size, usize alignment);
```

Unlike `kalloc()`, it returns `NULL` rather than panicking on an exhausted arena
or invalid alignment. `kalloc()` remains the trusted-kernel convenience wrapper
and still panics on failure.

This lets memory-object creation report allocation failure instead of turning a
recoverable tensor-backing request into a kernel panic.

## Tensor binding API

New tensor operations are:

```c
ai_tensor_status ai_tensor_bind_memory(
    ai_tensor *tensor,
    nx_handle_t memory_handle,
    u64 offset
);

ai_tensor_status ai_tensor_allocate_backing(
    ai_tensor *tensor,
    u64 alignment
);

bool ai_tensor_unbind_memory(ai_tensor *tensor);
nx_memory *ai_tensor_backing(const ai_tensor *tensor);
void *ai_tensor_data(ai_tensor *tensor);
```

Binding validates the memory handle and verifies that:

```text
backing_offset + tensor.storage_span_bytes <= memory.capacity_bytes
```

without performing overflow-prone addition.

A tensor stores only the generation-checked memory handle plus offset/capacity;
it does not embed a raw physical address.

## Tensor views

A view is not allowed to allocate private backing:

```text
AI_TENSOR_VIEW
    -> ai_tensor_allocate_backing()
    -> AI_TENSOR_ERR_VIEW_REQUIRES_EXISTING_BACKING
```

Instead it must bind to an existing memory object with an offset. This is the
first concrete step toward zero-copy aliases and slices.

Example:

```text
memory object: 4096 bytes

0                                                    4095
+-------------------------------------------------------+
| owner tensor data                                     |
+-------------------------------------------------------+
                ^
                |
                +--- view backing_offset = 64
```

Step 4 will add ownership/reference semantics so the memory object cannot be
retired while aliases still reference it.

## Device placement status

Real GPU/NPU allocators do not exist yet. If a tensor whose requested location
is GPU/NPU/etc. receives Step 3 backing, the bytes are still resident in the CPU
early heap and the memory object is marked:

```text
NX_MEMORY_FLAG_EMULATED_DEVICE
```

This prevents the current research skeleton from falsely claiming that CPU RAM
is actual HBM or NPU memory. Later placement code can replace that backend.

## Zero initialization and policy propagation

Tensor policy is projected into backing flags:

```text
AI_TENSOR_ZERO_INIT -> NX_MEMORY_FLAG_ZERO_INIT
AI_TENSOR_READONLY  -> NX_MEMORY_FLAG_READONLY
AI_TENSOR_PINNED    -> NX_MEMORY_FLAG_PINNED
non-CPU location    -> NX_MEMORY_FLAG_EMULATED_DEVICE
```

Zero-initialization covers the full resident allocation, not only the logical
tensor bytes.

## Accounting

`nx_memory_stats` records:

```text
created
live
requested_bytes
resident_bytes
peak_resident_bytes
allocation_failures
```

This distinction matters because a 128-byte tensor currently consumes a 4 KiB
resident page.

Tensor statistics remain logical-semantic statistics. Memory statistics are
resident-storage statistics. Keeping those domains separate is intentional.

## Tests

Two host-side test suites are available:

```bash
make test
```

They cover:

- tensor shape/layout metadata from Step 2;
- page-rounded memory allocation;
- page alignment;
- generation-checked `NX_OBJECT_MEMORY` lookup;
- zero initialization;
- tensor data access;
- view binding at a non-zero offset;
- refusal to privately allocate a view;
- rejection of undersized backing;
- resident/requested byte accounting.

Expected output:

```text
tensor metadata tests: PASS
tensor backing tests: PASS
```

The freestanding kernel is also compiled with:

```text
-Wall -Wextra -Werror
```

## Known limitations intentionally deferred

Step 3 does not implement:

- page reclamation/free lists;
- ownership;
- reference counts;
- pin counts;
- automatic tensor destruction;
- physical-memory-map parsing;
- arbitrary virtual mappings;
- actual GPU/NPU memory allocation;
- DMA/IOMMU mapping.

Those concerns must not be faked. The next phase adds ownership and reference
semantics on top of the now-real tensor-to-memory relationship.

## Exit condition

Step 3 is complete when a tensor can:

1. exist as metadata only;
2. receive an `NX_OBJECT_MEMORY` backing allocation;
3. resolve the backing through a kernel handle;
4. obtain a bounds-checked kernel data pointer;
5. share the same backing with a view at an offset;
6. distinguish requested tensor bytes from page-resident bytes.
