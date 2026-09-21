# Phase 3 — Step 1: Kernel Object Foundation

This step introduces a shared object model for Nexora kernel-managed resources.

## Goal

Tensors, work nodes, memory objects, devices, capability domains, and future AI-specific resources must not each invent their own identity and lifetime mechanism. They need one kernel-level foundation.

## Object identity vs handle

Nexora deliberately separates two concepts:

- **object ID** — monotonically assigned identity used for tracing, graph relationships, and diagnostics;
- **handle** — generation-checked registry reference used for safe lookup.

A handle encodes:

```text
63                              32 31                              0
+--------------------------------+--------------------------------+
|          generation            |            slot + 1            |
+--------------------------------+--------------------------------+
```

Handle `0` is permanently invalid. When a registry slot is retired its generation is incremented, so an old handle cannot accidentally resolve to a new object that later occupies the same slot.

## Base object

Every kernel-managed AI object can embed `nx_object` as its first field:

```c
typedef struct nx_object {
    nx_object_id_t id;
    nx_handle_t handle;
    const char *name;
    nx_object_type type;
    nx_object_state state;
    u32 flags;
} nx_object;
```

Tensor and work objects now do this directly.

## Lifecycle

The initial generic state machine is intentionally small:

```text
NEW -> LIVE -> QUIESCING -> DEAD
  \-> DEAD      \----------> DEAD
```

Rules:

1. registration creates a globally unique ID and generation-checked handle;
2. a registered object transitions from `NEW` to `LIVE`;
3. retirement invalidates the registry slot and handle;
4. a stale handle fails lookup after slot reuse;
5. the generic object layer does **not** yet implement reference counting — that is Phase 3 Step 4;
6. the generic object layer does **not** free backing memory — physical/tensor backing is added in later Phase 3 steps.

## Object types reserved now

- tensor
- work
- capability
- memory
- domain
- device

Only tensor and work objects are migrated in this step. The remaining types are reserved so future subsystems share the same ABI instead of adding incompatible identity systems.

## Registry properties

The first implementation uses a fixed-capacity registry of 1024 slots. This is appropriate for the current research kernel because it is deterministic, allocation-free, and easy to inspect while the kernel substrate is still evolving.

Later versions can replace this implementation with a radix tree, slab-backed table, or per-domain registry without changing object handles at higher layers.

## New invariants

For every live tensor or work node:

```text
object.id != 0
object.handle != 0
object.state == LIVE
lookup(object.handle, object.type) == &object
```

For every retired handle:

```text
lookup(old_handle, expected_type) == NULL
```

## Integration performed

`ai_tensor` now starts with `nx_object` instead of maintaining its own `id` and `name` fields.

`ai_work_node` now starts with `nx_object`, and work dependencies use the global object ID rather than a graph-local ID counter.

This is a prerequisite for producer/consumer edges between object types in later Phase 3 steps.

## Step 1 acceptance criteria

- object subsystem initializes during kernel boot;
- tensors register as `NX_OBJECT_TENSOR`;
- work nodes register as `NX_OBJECT_WORK`;
- object IDs are globally unique across those types;
- handles resolve through the central registry;
- type-mismatched handle lookup fails;
- stale handles are invalidated by generation changes;
- existing tensor/work demo still compiles and runs;
- no reference counting or tensor backing is introduced prematurely.
