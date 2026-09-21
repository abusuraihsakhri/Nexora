# Phase 3 Step 9 — Automatic Reclamation

## Goal

Step 9 connects the producer/consumer graph, tensor lifetime policy, and memory-object accounting into one reclamation subsystem.

The kernel can now reclaim temporary tensor backing immediately after final use, defer and retry reclamation when a transient condition blocks it, and perform a target-based pressure pass over graph-dead temporary and cached tensors.

## Architecture

```text
Work completes
     |
     v
consumer edge marked done
     |
     v
consumers_remaining == 0
     |
     v
Reclamation Manager
     |
     +--> immediate final-consumer reclaim
     |
     +--> deferred retry queue if pinned/transiently blocked
     |
     +--> target-based pressure scan
              |
              +--> graph-dead TEMPORARY first
              +--> safe CACHED second
```

The reclamation manager is implemented in:

```text
include/ai/reclaim.h
src/ai/reclaim.c
```

## Final-consumer reclamation

`ai_work_complete()` still owns consumer-edge completion. When the final consumer of a resident temporary tensor completes, it now calls:

```c
ai_reclaim_on_final_consumer(tensor, &deferred);
```

The manager delegates policy enforcement to the Step 5 lifetime engine using `AI_RECLAIM_FINAL_CONSUMER`.

Successful reclamation detaches the tensor from its backing memory object while preserving tensor identity and metadata:

```text
Tensor object: LIVE
shape/dtype:   preserved
object handle: preserved
backing:       released
residency:     RECLAIMED
```

## Deferred retry

A final-use reclaim can be temporarily unsafe. The primary current example is a transient object or backing pin.

Retryable failures are inserted into a bounded queue as non-owning, generation-checked tensor handles. The queue therefore does not create a reference cycle and cannot accidentally target a newer object that reused the same registry slot.

```text
final consumer
     |
     v
reclaim attempt
     |
     +--> success -> done
     |
     +--> pinned/transient failure
              |
              v
        deferred queue
              |
              v
        later retry pass
```

The scheduler calls `ai_reclaim_retry_deferred()` before selecting new work. Pressure passes also retry deferred entries first. A caller can invoke the retry API explicitly after removing a pin.

If the stored handle has become stale or the tensor has already lost its backing, the entry is dropped safely.

## Memory-pressure reclamation

The pressure API is:

```c
ai_reclaim_under_pressure(target_resident_bytes, &report);
```

The target is expressed in **actual live resident bytes**, not tensor logical bytes.

The pass uses this order:

1. graph-dead `TEMPORARY` tensors;
2. safe `CACHED` tensors.

It stops when the requested resident-byte target has been met or when no eligible candidate remains.

Within a class, the largest currently backed candidate is preferred, then object ID is used as a deterministic tie-break.

## Conservative graph-dead test

Pressure reclamation deliberately does not treat every temporary tensor as disposable.

A temporary tensor must have:

- resident backing;
- zero remaining consumers;
- no producer that can still write it; and
- evidence that it participated in graph dataflow, either through a consumer edge or a producer edge.

This protects newly created, unattached temporary buffers that may still be in direct caller use.

A cached tensor may be pressure-evicted without graph edges because its lifetime class explicitly declares it as evictable/reconstructible policy data. If it does participate in a graph, pending consumers or a live producer still protect it.

## Protected objects

Pressure passes do not reclaim:

- persistent tensors;
- external tensors;
- shared tensors by generic pressure policy;
- tensors with remaining consumers;
- outputs whose producer has not completed;
- pinned tensor objects;
- pinned memory objects.

These protections are layered on top of the Step 5 lifetime engine, which remains the final policy authority.

## Cache-only eviction

For explicit cache trimming, Step 9 adds:

```c
ai_reclaim_evict_cache(target_resident_bytes, &report);
```

This uses `AI_RECLAIM_CACHE_EVICTION` and considers only safe cached tensors.

## Accounting

`ai_reclaim_report` records:

```text
target_resident_bytes
resident_before
resident_after
binding_bytes_released
resident_bytes_reclaimed
candidates_scanned
attempts
successes
denied
skipped_live
skipped_pinned
target_met
```

The distinction between binding bytes and resident bytes is intentional. Multiple tensors can reference one memory object. Removing one binding does not necessarily remove any resident storage until the final memory reference is released.

Global reclamation statistics additionally track final-consumer events, deferrals, retry passes, pressure runs, cache eviction runs, and actual resident bytes reclaimed.

## Tensor registry access

Step 9 adds two bounded registry helpers:

```c
ai_tensor_at(index)
ai_tensor_lookup(handle)
```

The pressure scanner uses `ai_tensor_at()`. Deferred retry uses generation-checked `ai_tensor_lookup()`.

## Current limitation: logical residency vs reusable pages

`NX_OBJECT_MEMORY` still uses the monotonic bootstrap early heap. Destroying the final memory-object reference therefore removes that allocation from **live resident accounting**, but cannot move the bump pointer backward or return the physical page to a free list.

Consequently:

```text
resident_bytes decreases        yes
early_heap_used decreases       no
page reusable by new allocation no
```

The reclamation policy and object semantics are intentionally independent of the backend. Once Nexora has a real page-frame allocator, the memory-object destructor can return pages without changing tensor, graph, or reclamation APIs.

## Test coverage

`tests/automatic_reclamation_test.c` verifies:

- pin-induced final-consumer deferral;
- generation-safe deferred retry;
- deferred queue drain after unpin;
- pressure protection of graph-live temporary tensors;
- pressure protection of persistent tensors;
- reclamation of a completed, unused temporary output;
- temporary-before-cache pressure ordering;
- largest-cache selection;
- cache-only eviction;
- actual live resident-byte target accounting;
- cleanup of all tensor and memory objects.

Run:

```bash
make test-reclaim
```

or the complete suite:

```bash
make test
```
