# Phase 4 — Step 7: Reference counting and lifetime management

## Goal

Connect the lifetime of domain handles, tensor metadata, shared backing storage,
and mappings into one explicit ownership graph.

Step 6 allowed two domains to map the same backing. Step 7 answers the harder
question: **when may the tensor and its backing stop existing?**

The rule is now reference based rather than owner-name based.

```text
creator ref ----\
handle A --------+--> ai_tensor refcount
handle B --------+
mapping A -------+
mapping B -------/
                     |
                     v
               ai_tensor backing ref
                     |
creator backing -----+--> ai_tensor_backing refcount
mapping A -----------+
mapping B -----------/
```

A tensor is retired after its last legitimate tensor reference disappears. A
backing is retired after its last backing reference disappears. Every mapping
owns both a tensor reference and a backing mapping reference.

## Lifetime states

Managed tensors:

```text
LIVE -- final put --> RETIRED
```

Managed backings:

```text
LIVE -- final put --> RETIRED
```

Retirement is terminal. `get()` on a retired object fails, preventing accidental
resurrection of an object whose namespace references have already disappeared.

## Creator references

`ai_tensor_create()` returns a managed tensor with:

```text
tensor.refcount = 1
```

That is the caller/creator reference.

`ai_backing_create_ram()` similarly returns:

```text
backing.refcount = 1
```

Callers must eventually release those references with `ai_tensor_put()` and
`ai_backing_put()`.

This makes raw creator pointers explicit participants in the lifetime model
rather than invisible references outside the kernel's accounting.

## Tensor-to-backing ownership

`ai_tensor_attach_backing()` now **retains** the backing. Attachment does not
consume the caller's creator reference.

Example:

```text
create backing             refcount = 1
attach to tensor            refcount = 2
caller drops creator ref    refcount = 1
retire tensor               refcount = 0 -> backing retires
```

A tensor therefore cannot retain a dangling backing pointer.

## Handle ownership

For managed tensor and backing objects, every occupied handle entry owns one
reference.

```text
install handle   -> object get
share handle     -> recipient install -> object get
close handle     -> invalidate entry -> object put
```

`TRANSFER` remains destination-first:

```text
install destination handle  -> +1 ref
close source handle          -> -1 ref
```

The net reference count therefore remains unchanged across a successful move.
If destination creation fails, the source is untouched. If source invalidation
fails, the destination handle is closed as rollback.

The handle token still contains no raw pointer and no rights bits. Lifetime
ownership remains kernel-side in the handle-table entry.

## Mapping ownership

A successful mapping now acquires:

1. one tensor reference, and
2. one backing mapping reference.

The backing mapping reference increments both:

```text
backing.refcount
backing.mapping_count
```

On unmap the mapping namespace entry is invalidated first, then both references
are released.

This ordering provides the important property:

> Closing every tensor handle does not invalidate an already-established
> mapping.

A mapping can therefore remain valid during domain quiescence while handles are
being drained.

## Final-reference path

A representative two-domain sequence is:

```text
create tensor                         tensor refs = 1
create backing                        backing refs = 1
attach backing                        backing refs = 2
install owner handle                  tensor refs = 2
share recipient handle                tensor refs = 3
map owner                             tensor refs = 4, backing refs = 3
map recipient                         tensor refs = 5, backing refs = 4

drop tensor creator ref               tensor refs = 4
drop backing creator ref              backing refs = 3
close owner handle                    tensor refs = 3
close recipient handle                tensor refs = 2
unmap owner                           tensor refs = 1, backing refs = 2
unmap recipient                       tensor refs = 0
                                       -> tensor retires
                                       -> tensor releases backing ref
                                       -> mapping releases backing ref
                                     backing refs = 0
                                       -> backing retires
```

This is the core Step 7 acceptance invariant.

## Registry retirement

When a managed tensor or backing reaches zero references it is removed from its
live registry and marked `RETIRED`.

Consequences:

- it no longer counts toward `ai_tensor_count()` / `ai_backing_count()`;
- it cannot acquire new references;
- it cannot be installed into a new tensor handle;
- retired backing address helpers return no address;
- stale handle and mapping generations remain invalid.

## Double-release protection

`ai_tensor_put()` and `ai_backing_put()` return `false` if the object is already
retired or has no reference to release. They do not decrement through zero.

This gives deterministic protection against refcount underflow in the current
single-core prototype.

Structural lifetime invariants that should be impossible through public APIs
still panic, for example trying to retire a backing while `mapping_count != 0`.

## Compatibility with early stack tensors

Earlier host tests construct small tensor structs directly on the stack. Those
objects have `managed == false` and therefore use no-op get/put semantics.

All tensors returned by `ai_tensor_create()` are managed. New kernel code should
prefer managed tensors; the unmanaged path exists only to preserve the earlier
prototype tests while the object model evolves.

## Physical reclamation boundary

Step 7 performs **logical reclamation** correctly, but the kernel still uses the
Phase 1 bump allocator:

```text
kalloc() -> allocate only
free()   -> not available yet
```

Therefore retired tensor/backing objects leave the live registries and cannot be
used again, but their underlying early-heap bytes cannot yet be returned to a
physical page allocator.

True physical page recycling requires the planned page-frame allocator/kernel
heap. This package deliberately does not claim physical reclamation that the
current allocator cannot provide.

The lifetime graph is nevertheless the prerequisite for safe future `free()`:
the allocator will know exactly when the final legitimate reference has gone.

## Concurrency boundary

Reference counters are plain integer operations in Step 7. This matches the
current single-core prototype.

Phase 4 Step 8 will add the synchronization model for:

- atomic reference transitions;
- handle/mapping table locking;
- revocation races;
- final-put races;
- stale-handle/mapping use under concurrent execution.

## Primary APIs

```c
bool ai_tensor_get(ai_tensor *tensor);
bool ai_tensor_put(ai_tensor *tensor);
bool ai_tensor_is_live(const ai_tensor *tensor);
u64  ai_tensor_refcount(const ai_tensor *tensor);

bool ai_backing_get(ai_tensor_backing *backing);
bool ai_backing_put(ai_tensor_backing *backing);
bool ai_backing_is_live(const ai_tensor_backing *backing);
u64  ai_backing_refcount(const ai_tensor_backing *backing);

bool ai_backing_mapping_acquire(ai_tensor_backing *backing);
bool ai_backing_mapping_release(ai_tensor_backing *backing);
```

Handle and mapping APIs acquire/release these references internally.

## Host acceptance test

`tests/lifetime_host_test.c` verifies:

- creator references;
- tensor-retained backing reference;
- handle-retained tensor references;
- SHARE reference growth;
- mapping-retained tensor/backing references;
- mappings surviving handle closure;
- final-map teardown causing tensor retirement;
- tensor retirement releasing its backing reference;
- final backing retirement;
- live-registry removal;
- stale mapping rejection;
- retired-object resurrection rejection;
- double-put rejection;
- domain destruction after reference drain.

## Next

Phase 4 Step 8 — revocation, stale-reference concurrency, and SMP-safe lifetime
synchronization.
