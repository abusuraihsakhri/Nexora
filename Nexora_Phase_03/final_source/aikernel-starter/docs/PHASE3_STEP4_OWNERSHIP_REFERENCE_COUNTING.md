# Phase 3 Step 4 — Ownership and Reference Counting

## Objective

Step 4 makes kernel-object lifetime explicit. A tensor may now share a memory
object without risking use-after-retire: every binding owns a strong reference,
pins can delay destruction, and the final release deterministically invokes an
object-specific destructor before the registry slot is recycled.

This step does **not** yet add graph-derived tensor lifetimes. It establishes the
mechanism that later graph/lifetime code can safely drive.

## Object lifetime model

Every `nx_object` now contains:

```text
owner_id
strong_refs
pin_count
destroy callback
state
```

Registration creates exactly one strong reference: the creator/owner reference.
The normal lifecycle is:

```text
NEW -> LIVE -> QUIESCING -> DEAD
```

The transition to `QUIESCING` occurs when the final strong reference is released.
If no pins remain, destruction is immediate. If pins remain, the object stays
quiescing until the final pin is removed.

A quiescing object:

- remains lookup-addressable so the holder of an existing pin can unpin it;
- cannot acquire new strong references;
- therefore cannot be resurrected.

After finalization, the registry slot generation changes, so stale handles fail.

## Ownership

`nx_owner_id_t` is currently a 64-bit owner identifier.

```text
NX_OWNER_KERNEL = 0
NX_OWNER_NONE   = invalid/sentinel owner
```

Ownership is currently policy/attribution metadata, not itself a strong
reference to a domain object. `nx_object_transfer_owner()` uses an expected-owner
argument so accidental ownership replacement is rejected.

Future domain/capability work can bind this ID to an actual protection domain.

## Strong references

Core operations:

```c
nx_object_retain(handle);
nx_object_release(handle);
```

The final release does not simply erase the registry entry. It first runs the
object-specific destructor, then marks the object dead, clears the slot, and
advances the slot generation.

This is important for composite kernel objects. A tensor destructor must release
its backing memory before the tensor itself is retired.

## Pinning

Pinning is deliberately independent from strong references:

```c
nx_object_pin(handle);
nx_object_unpin(handle);
```

A pin means "do not finalize this object yet." It does not permit new ownership
or resurrection. This lets future DMA, device submission, or mapping code hold a
short-lived stability guarantee without fabricating an ownership relationship.

`AI_TENSOR_PINNED` currently causes a bound backing-memory object to receive one
object pin for the lifetime of that tensor binding.

The existing `NX_MEMORY_FLAG_PINNED` remains physical-placement policy metadata;
the object `pin_count` is the lifetime mechanism.

## Tensor-to-memory reference ownership

Before Step 4 a tensor stored a memory handle but did not keep the memory object
alive. Step 4 changes binding to:

```text
ai_tensor_bind_memory()
        |
        +--> validate handle/range
        +--> retain memory object
        +--> optionally pin memory object
        +--> publish binding
```

Unbinding performs the reverse operation:

```text
ai_tensor_unbind_memory()
        |
        +--> remove optional pin
        +--> release strong memory reference
        +--> clear tensor binding metadata
```

`ai_tensor_allocate_backing()` follows an explicit handoff:

```text
memory create       refs = 1  (creator)
tensor bind         refs = 2  (creator + tensor)
creator handoff     refs = 1  (tensor only)
...
tensor destruction  refs = 0  -> memory destructor -> DEAD
```

This makes private tensor backing deterministic rather than effectively leaked
at the object-model level.

## Shared backing

Two tensor views can safely share one backing object:

```text
                  NX_OBJECT_MEMORY
                  strong_refs = 2
                    /        \
                   /          \
              Tensor A     Tensor B
```

After the creator reference is dropped, the backing stays alive until both
tensors release their bindings. Destroying one tensor cannot invalidate the
other tensor's storage.

## Destructors

`nx_object_register_ex()` accepts an optional destroy callback. Step 4 uses it
for:

- `NX_OBJECT_MEMORY`: remove active resident/requested-byte accounting and clear
  the bootstrap address;
- `NX_OBJECT_TENSOR`: release backing references, remove the tensor from its
  registry, and decrement live/logical tensor accounting.

The bootstrap bump allocator remains monotonic. Therefore object destruction
makes backing **logically non-resident**, but does not yet return physical pages
to a reusable free list. `early_heap_used()` still reflects committed bootstrap
arena consumption. A future page-frame allocator will make physical reclamation
real without changing the lifetime API.

## New public APIs

### Generic objects

```c
nx_object_register_ex(..., owner_id, destroy);
nx_object_retain(handle);
nx_object_release(handle);
nx_object_pin(handle);
nx_object_unpin(handle);
nx_object_transfer_owner(handle, expected_owner, new_owner);
nx_object_ref_count(object);
nx_object_pin_count(object);
```

### Memory objects

```c
nx_memory_try_create_owned(...);
nx_memory_create_owned(...);
nx_memory_retain(handle);
nx_memory_release(handle);
nx_memory_pin(handle);
nx_memory_unpin(handle);
```

### Tensors

```c
ai_tensor_try_create_owned(...);
ai_tensor_create_owned(...);
ai_tensor_retain(tensor);
ai_tensor_release(tensor);
ai_tensor_pin(tensor);
ai_tensor_unpin(tensor);
```

## Safety invariants

Step 4 enforces these invariants:

1. A live object starts with one creator reference.
2. A tensor cannot publish a backing binding unless it first retains the memory object.
3. Tensor destruction releases its backing reference.
4. A pinned zero-reference object becomes quiescing rather than dead.
5. A quiescing object cannot be retained again.
6. The final unpin of a zero-reference object triggers deterministic finalization.
7. A stale generation-checked handle cannot resolve after retirement.
8. Shared memory remains live until the final strong reference is released.

## Tests

Run:

```bash
make test
```

The new `ownership_refcount_test` validates:

- creator references;
- pin-delayed destruction;
- no resurrection from `QUIESCING`;
- destructor execution;
- stale-handle rejection;
- compare-and-transfer ownership;
- two tensors sharing one memory object;
- pinned tensor backing;
- final-reference memory retirement;
- tensor/memory destruction statistics.

## Step boundary

Step 4 provides **mechanism**, not AI graph policy. The next step can classify
objects by intended lifetime (temporary, persistent, cached, shared, external)
and map those policies onto the ownership/reference primitives implemented here.
