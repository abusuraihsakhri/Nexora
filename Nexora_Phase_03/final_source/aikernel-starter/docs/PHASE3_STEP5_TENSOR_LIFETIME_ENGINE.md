# Phase 3 Step 5 — Tensor Lifetime Engine

## Objective

Step 5 separates **tensor semantic lifetime** from raw object reference counting.

Reference counting answers:

> Is any kernel user still holding this object?

The lifetime engine answers a different question:

> Given what this tensor represents, under which conditions may Nexora discard its backing storage while keeping the tensor object itself valid?

This distinction is necessary for graph-aware reclamation, cache eviction, persistent model residency, shared tensors, and imported/external memory.

## Lifetime classes

Every tensor now has one canonical `ai_tensor_lifetime`:

| Class | Intended use | Automatic reclaim policy |
|---|---|---|
| `TEMPORARY` | activations, scratch, request-local tensors | final consumer, memory pressure, explicit |
| `PERSISTENT` | model weights or state intended to remain resident | explicit only |
| `CACHED` | recomputable or reloadable cache entries | memory pressure, cache eviction, explicit |
| `SHARED` | tensors intentionally shared by multiple kernel users | explicit only, and only when one strong tensor reference remains |
| `EXTERNAL` | imported storage whose producer/owner is outside the tensor | explicit detach only |

`AUTO` exists only as a compatibility input. It is normalized at tensor creation and is never stored as the final policy.

## Residency state

The tensor object now tracks storage state independently:

```text
UNBACKED -> RESIDENT -> RECLAIMED
                    \-> UNBACKED   (manual unbind)
RECLAIMED -> RESIDENT              (repopulation/reallocation)
```

A reclaimed tensor retains its:

- identity and generation-safe handle
- shape and strides
- dtype and tensor class
- owner
- lifetime policy
- dependency metadata added in future steps

Only its backing reference is removed.

This permits cached or temporary tensors to be re-backed without rebuilding semantic metadata.

## Reclaim reasons

The engine records why storage is being discarded:

```c
AI_RECLAIM_FINAL_CONSUMER
AI_RECLAIM_MEMORY_PRESSURE
AI_RECLAIM_CACHE_EVICTION
AI_RECLAIM_EXPLICIT
```

The policy matrix is intentionally strict. A persistent tensor does not become pressure-reclaimable merely because the allocator is short of memory. An external tensor is not detached by generic pressure handling. A shared tensor cannot be stripped while another strong user still references the tensor object.

## Pins override policy

Even when a lifetime class would normally permit reclaim, reclamation is denied if:

- the tensor carries `AI_TENSOR_PINNED`,
- the tensor object has active pins, or
- its backing memory object has active pins.

This preserves the Step 4 distinction between ownership and temporary non-movability/non-finalizability.

## Backing reclamation

The main API is:

```c
ai_lifetime_status ai_tensor_lifetime_reclaim(
    ai_tensor *tensor,
    ai_reclaim_reason reason
);
```

A successful reclaim:

1. validates the tensor and reclaim reason,
2. checks the lifetime policy,
3. checks tensor and backing pins,
4. checks shared-reference constraints,
5. removes the tensor's strong backing reference,
6. destroys the memory object if that was its final strong reference,
7. marks the tensor `RECLAIMED`, and
8. records reclaim statistics.

For shared or external memory, dropping the tensor binding may **not** destroy the memory object because another owner can still retain it. The lifetime statistics therefore distinguish:

- `binding_bytes_released` — bytes no longer bound by reclaimed tensors,
- `resident_bytes_reclaimed` — actual reduction in live memory-object resident accounting.

That distinction prevents false claims of physical-memory savings.

## Compatibility flags

The older `AI_TENSOR_PERSISTENT` and `AI_TENSOR_EPHEMERAL` flags remain accepted for compatibility, but lifetime class is now canonical.

Creation normalizes them:

```text
TEMPORARY  -> EPHEMERAL
PERSISTENT -> PERSISTENT
CACHED     -> neither legacy lifetime flag
SHARED     -> neither legacy lifetime flag
EXTERNAL   -> EXTERNAL
```

Contradictory explicit lifetime/flag combinations are rejected.

## Statistics

`ai_lifetime_stats` records:

```text
live_by_class[]
reclaim_attempts
reclaim_successes
reclaim_denied
binding_bytes_released
resident_bytes_reclaimed
```

These metrics are intended to feed the Phase 3 graph-lifetime experiment.

## What Step 5 does not do

Step 5 does not yet know which work node is the final consumer. That belongs to Step 6, where producer/consumer edges become kernel-visible.

It also does not return bootstrap arena pages to a physical free list. `NX_OBJECT_MEMORY` live-resident accounting can fall to zero when an object is destroyed, but the Step 3 early bump allocator remains monotonic. A later PMM backend can make those same lifetime decisions physically reclaim pages without changing this policy API.

## Step 6 integration point

Once consumer counts exist, Step 6 can perform:

```c
if (tensor->consumers_remaining == 0) {
    ai_tensor_lifetime_reclaim(tensor, AI_RECLAIM_FINAL_CONSUMER);
}
```

Only `TEMPORARY` tensors will accept that reason. Persistent, cached, shared, and external tensors retain their distinct policies.
