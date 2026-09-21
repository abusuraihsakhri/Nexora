# Phase 4 — Step 8: Revocation and concurrency hardening

## Goal

Make the Phase 4 shared-tensor capability path linearizable under concurrent
CPUs/threads and close the major stale-reference races left by Step 7.

Step 8 adds four related mechanisms:

1. spinlock-protected mutable namespaces (handle and mapping tables),
2. atomic tensor/backing/domain lifetime/accounting transitions,
3. immediate handle revocation plus generation-safe reap/reuse,
4. explicit reference-pinned access APIs for concurrent users.

The kernel can now distinguish **namespace validity** from **object lifetime**.
A handle may be revoked immediately while a thread that acquired an object pin
before revocation safely finishes its operation.

## Synchronization primitives

New headers:

```text
include/kernel/spinlock.h
include/kernel/atomic.h
```

`ai_spinlock` uses GCC/Clang atomic builtins with acquire/release ordering and an
x86 `pause` loop while contended. The primitive is intentionally small and
freestanding; it does not depend on libc or POSIX threads.

The host stress suite uses pthreads only to exercise the same kernel code under
actual concurrent execution.

## Handle-table locking

Each `ai_handle_table` now owns a spinlock:

```text
ai_handle_table
 ├── entries[]
 ├── live_count
 ├── revoked_count
 └── lock
```

Installation, close, rights attenuation, revocation, reap, and generation
changes are serialized by this lock.

A token can therefore transition only once through the relevant state:

```text
FREE
  ↓ install
LIVE
  ├──────────── close ────────────→ FREE(new generation)
  │
  └──────────── revoke ─→ REVOKED ─→ reap/close ─→ FREE(new generation)
```

Generation advancement still occurs when the slot is physically detached from
the namespace. A stale token cannot become valid when the slot is reused.

## Revocation semantics

`ai_handle_revoke(domain, handle)`:

- marks that exact handle entry revoked,
- removes all rights from the entry,
- blocks new `resolve`/`acquire` operations immediately,
- does not prematurely release a reference that another thread may still need,
- leaves final entry cleanup to close or `ai_handle_reap_revoked()`.

This is **local handle revocation**, not lineage-wide revocation. If a handle was
already shared into another domain, revoking one token does not implicitly scan
and revoke every independently delegated token. A future revocation-domain or
capability-lineage object can provide transitive revocation if required by the
security model.

## Pinned handle acquisition

The old `ai_handle_resolve()` returns a non-owning pointer and remains useful for
diagnostics and single-threaded assertions. It cannot make a raw pointer safe
after the table lock is released.

Concurrent kernel paths now use:

```c
void *ai_handle_acquire(...);
bool ai_handle_release_object(void *object, ai_handle_object_type type);
```

For managed tensors/backings, acquisition increments the object refcount while
the handle-table lock is still held. This produces a stable object pin.

The race is therefore linearizable:

```text
CPU 0: acquire(handle) ── pin object ── unlock
CPU 1: revoke(handle)  ──────────────── deny future acquisitions
CPU 1: reap(handle)    ── drop handle-owned ref
CPU 0: continue safely using pinned object
CPU 0: release pin     ── possibly perform final retirement
```

If revocation wins first, CPU 0 cannot acquire the pin.

## SHARE and TRANSFER races

Cross-domain delegation now takes a temporary source-object pin while examining
the source handle.

`SHARE`:

1. lock source table,
2. validate rights and pin object,
3. unlock source table,
4. install target handle (which owns its own ref),
5. release temporary pin.

`TRANSFER` additionally revalidates the source after destination installation.
The source entry is detached only if the exact token is still live and still
carries the required authority. If close/revoke/rights attenuation wins the
race, the destination installation is rolled back.

No path simultaneously holds two domains' handle-table locks, avoiding
cross-domain lock-order cycles in this phase.

## Atomic final-reference handling

Tensor and backing refcounts now use compare-and-exchange loops.

The key invariant is:

```text
refcount > 0  => get may succeed
refcount == 0 => get must fail forever
```

A final `put` changes `1 -> 0`. Only that thread performs the terminal
`LIVE -> RETIRED` transition and registry removal.

A concurrent `get` can linearize before the final `put` (and keep the object
alive), or after it (and fail), but cannot resurrect a zero-reference object.

Backing `mapping_count` is also atomic. Mapping acquire first takes a backing
reference, then increments `mapping_count`; release decrements the mapping count
before dropping its backing reference. A backing cannot retire while a live
mapping-owned reference remains.

## Mapping-table locking and mapping views

Every `ai_mapping_table` now has a spinlock. Map, unmap, generation advancement,
virtual-address allocation, and live-count changes occur under that lock.

Step 8 adds a concurrent-safe view API:

```c
bool ai_mapping_acquire_view(
    const ai_domain *domain,
    ai_mapping_id_t mapping,
    ai_mapping_view *view
);

void ai_mapping_release_view(ai_mapping_view *view);
```

A view is a copied mapping descriptor plus independent tensor/backing pins.
Therefore an unmap can invalidate the mapping ID while an already-acquired view
continues to use the underlying object/storage safely.

`ai_mapping_translate()` now performs lookup and translation while holding the
mapping-table lock.

`ai_mapping_resolve()`, `ai_mapping_kernel_address()`, and similar raw snapshot
helpers remain non-owning. Code that must retain mapping state across a possible
concurrent unmap should use an acquired view.

## Domain state and accounting

Domain lifecycle state now uses atomic compare-and-exchange:

```text
NEW --CAS--> ACTIVE --CAS--> QUIESCING --> DEAD
```

Memory, tensor-count, and work-count accounting use atomic CAS loops. Handle and
mapping creation recheck domain state after taking their table locks, producing
a clean quiesce linearization point. Once quiescing wins, new handles/mappings
are rejected while existing resources can still be drained.

The domain registry and early bump allocator are also spinlock protected, which
removes avoidable host/SMP races in object/domain creation.

## Concurrency stress test

New test:

```text
tests/concurrency_host_test.c
```

It covers:

- 8-thread tensor `get`/`put` stress,
- exactly-once concurrent handle close,
- revoke + reap with a pre-existing pinned tensor reference,
- stale-generation rejection after revoked-slot reuse,
- mapping-view lifetime after concurrent-style unmap,
- repeated TRANSFER-vs-REVOKE races with rollback validation.

The test is run normally and under ThreadSanitizer.

## Explicit boundaries

Step 8 does **not** claim full production SMP security yet.

Current limitations:

- hardware per-domain page tables/user-mode isolation are still future work,
- the early bump allocator still cannot physically free retired pages/objects,
- `WORK` and `GENERIC` handle object types do not yet have managed lifetime
  hooks; pinned lifetime guarantees currently apply to managed TENSOR/BACKING
  objects,
- local handle revocation is not transitive capability-lineage revocation,
- the spinlock is not yet an IRQ-save/preemption-aware lock class,
- kernel SMP startup and cross-CPU interrupt infrastructure are not yet present;
  concurrency is validated with the host pthread harness.

These boundaries are deliberate and are documented rather than hidden.

## Acceptance result

Step 8 is complete when:

- handle/mapping mutations are lock protected,
- tensor/backing final-reference races are atomic,
- revoked handles immediately deny new authority,
- stale generations stay invalid after reap/reuse,
- an acquired object pin survives later revocation,
- an acquired mapping view survives later unmap,
- transfer/revoke races cannot duplicate or lose authority,
- legacy Step 3–7 host tests remain green,
- ASan/UBSan and ThreadSanitizer tests pass.

The next phase step is **Step 9 — adversarial security/correctness test suite**.
