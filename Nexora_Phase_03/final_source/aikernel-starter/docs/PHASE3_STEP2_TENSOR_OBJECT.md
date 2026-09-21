# Phase 3 — Step 2: Tensor Object Redesign

Status: **implemented**

This step turns the original tensor record into a kernel-grade metadata object that can safely describe AI storage before any physical backing is attached.

## Goals

The tensor layer now provides:

- explicit tensor classes
- shape and byte-stride metadata
- element-count calculation
- overflow-safe size calculation
- contiguous and explicit-strided layouts
- logical byte size versus physical storage span
- richer semantic flags
- a non-panicking validated creation API
- tensor statistics
- placeholders for a future `NX_OBJECT_MEMORY` backing handle

The key separation is now:

```text
Tensor identity / semantics
        |
        +-- dtype
        +-- class
        +-- shape
        +-- strides
        +-- logical size
        +-- layout
        +-- locality hint
        +-- policy flags
        |
        `-- backing-memory handle  <-- Step 3 binds this
```

A tensor is therefore no longer equivalent to an allocation.

## Tensor classes

`ai_tensor_class` currently distinguishes:

```text
generic
input
output
weight
activation
kv-cache
gradient
scratch
```

The class is descriptive metadata. Later phases can use it for placement, lifetime, reclaim, prefetch, admission control, and scheduling policy without inferring intent from names or allocation patterns.

## Shape and strides

Each tensor stores:

```c
u32 ndim;
u64 shape[AI_MAX_DIMS];
u64 stride_bytes[AI_MAX_DIMS];
```

Strides are represented in **bytes**, rather than elements. This makes the metadata directly useful to memory mapping and accelerator-transfer code without re-deriving byte offsets from dtype information.

If the caller does not provide strides, the constructor creates canonical row-major contiguous strides.

For example, a contiguous `f16 [1, 4096]` tensor has:

```text
shape        = [1, 4096]
stride_bytes = [8192, 2]
logical size = 8192 bytes
storage span = 8192 bytes
```

An explicit `f32 [2, 3]` tensor with:

```text
stride_bytes = [16, 4]
```

has:

```text
logical size = 24 bytes
storage span = 28 bytes
layout       = strided
```

The distinction is important: logical tensor payload and address span are not always equal.

## Overflow safety

Tensor creation no longer performs unchecked arithmetic such as:

```c
elements *= shape[i];
bytes = elements * dtype_size;
```

Every multiplication and storage-span addition is checked against the maximum `u64` value before allocation or object registration.

Creation can fail with specific status values including:

```text
shape overflow
byte-size overflow
zero extent
invalid rank
invalid dtype
invalid stride
invalid flags
registry full
```

This is a kernel correctness requirement. Malformed or adversarial tensor descriptors must not wrap to small allocations.

## Creation APIs

The normal convenience API remains available:

```c
ai_tensor *ai_tensor_create(...);
```

It constructs a canonical contiguous tensor and panics on invalid kernel-internal metadata.

A validated API is now also available:

```c
ai_tensor_status ai_tensor_try_create(
    const ai_tensor_desc *desc,
    ai_tensor **out_tensor
);
```

This is the path future syscall and runtime interfaces should use because invalid user-provided metadata can be rejected without crashing the kernel.

## Semantic flags

The tensor flags now include:

```text
PERSISTENT
EPHEMERAL
READONLY
PINNED
CONTIGUOUS
VIEW
EXTERNAL
ZERO_INIT
```

`CONTIGUOUS` is derived by the constructor from actual stride metadata rather than trusted blindly from the caller.

Some conflicting combinations are rejected. For example, a tensor cannot simultaneously be persistent and ephemeral, and view/external tensors cannot request kernel zero-initialization in this metadata-only stage.

## Memory-backing boundary

Each tensor now carries:

```c
nx_handle_t backing_memory;
u64 backing_offset;
u64 backing_capacity;
```

At Step 2 these fields are initialized as unbound:

```text
backing_memory   = NX_INVALID_HANDLE
backing_offset   = 0
backing_capacity = 0
```

Step 3 will introduce the actual memory object and bind tensors to physical/virtual backing through the handle instead of embedding raw addresses in tensor metadata.

This separation is intentional because multiple tensors may eventually refer to one memory object, including views, aliases, shared tensors, and subregions.

## Statistics

The tensor subsystem now records:

```text
created
live
logical_bytes
peak_logical_bytes
```

These are metadata-level logical-byte statistics. They are **not yet physical-memory statistics**. Step 3 will add backing allocation accounting so the two can be compared.

## Boot self-test

The kernel boot path now checks two important cases.

### Overflow rejection

A shape equivalent to:

```text
[UINT64_MAX, 2]
```

must return `AI_TENSOR_ERR_SHAPE_OVERFLOW` without allocating or registering an object.

### Non-contiguous tensor metadata

A `f32 [2,3]` tensor with byte strides `[16,4]` must report:

```text
logical_bytes     = 24
storage_span_bytes = 28
layout             = strided
```

A failure causes an early kernel panic.

## Current invariants

For every successfully created tensor:

1. rank is between 1 and `AI_MAX_DIMS`;
2. every shape extent is non-zero;
3. dtype has a known non-zero element size;
4. element count and logical byte size fit in `u64`;
5. explicit strides are dtype-aligned;
6. storage-span arithmetic fits in `u64`;
7. the object is registered as `NX_OBJECT_TENSOR`;
8. contiguous status is derived from the actual layout;
9. no physical-memory backing is implied merely by tensor creation.

## Deferred deliberately to Step 3+

This step does **not** yet provide:

- page allocation for tensor contents
- virtual mappings
- device DMA mappings
- actual pin/unpin mechanics
- physical residency migration
- ownership/reference counting
- tensor destruction/reclamation
- producer/consumer lifetime tracking

Those features depend on the tensor metadata being correct first.

## Step 3 target

Phase 3 Step 3 will add a kernel memory object and connect tensor objects to real backing so we can distinguish:

```text
logical tensor bytes
physical committed bytes
mapped bytes
resident location
allocation alignment
backing ownership
```

That is the point where Nexora can begin measuring AI-aware memory behavior rather than metadata alone.

## Host-side metadata test

The repository also includes a host-side test that exercises tensor metadata without GRUB or QEMU:

```bash
make test-tensor
```

It checks contiguous strides, strided storage-span calculation, handle lookup, overflow rejection, zero-extent rejection, conflicting flags, and tensor statistics.
