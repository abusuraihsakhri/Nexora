# Phase 4 — Step 9: Adversarial security and correctness validation

## Goal

Attack the Phase 4 shared-tensor capability path before benchmarking it.
Step 9 is not a feature-expansion step; it is a validation and hardening gate for
Steps 2–8.

The test suite treats handles, mapping IDs, rights, lifetimes, table capacity,
and concurrent teardown as hostile inputs rather than cooperative API usage.

## Adversarial coverage

### Handle/capability namespace

`tests/security_host_test.c` performs:

- the full non-zero 6-bit rights matrix,
- object-type mismatch checks,
- monotonic-rights attenuation checks,
- unknown-right-bit rejection,
- 64 single-bit token corruptions,
- 100,000 deterministic pseudo-random forged handle tokens,
- cross-domain copied-token rejection,
- 64-entry handle-table exhaustion,
- revoked-entry occupancy and reap behavior,
- stale-token rejection after slot reuse,
- terminal generation-boundary injection.

### Mapping namespace

The same suite performs:

- invalid protection combinations,
- unaligned ranges,
- out-of-bounds ranges,
- 64 single-bit mapping-ID corruptions,
- 50,000 deterministic pseudo-random forged mapping IDs,
- cross-domain mapping-ID rejection,
- 64-entry mapping-table exhaustion,
- stale-ID rejection after slot reuse,
- terminal mapping-generation injection,
- per-domain virtual-window exhaustion checks,
- domain-ID virtual-address overflow checks.

### Lifetime/refcount boundaries

White-box boundary tests verify that:

- tensor/backing `get` rejects `UINT64_MAX` refcounts,
- tensor/backing `get`/`put` reject zero-reference underflow/resurrection,
- backing `mapping_count == UINT64_MAX` rejects a new mapping reference without
  leaking the temporary backing reference,
- domain memory accounting rejects arithmetic overflow.

These white-box mutations are test-only fault injections; production callers do
not write the internal counters directly.

## Race matrix

`tests/race_matrix_host_test.c` repeatedly exercises the ambiguous ordering
boundaries:

```text
close  vs revoke
close  vs acquire(pin)
unmap  vs acquire_view
unmap  vs unmap (8 contenders)
```

The accepted outcomes are defined by linearization rather than by thread order.
Examples:

- close vs acquire: the acquire either fails because close won first, or returns
  a stable object pin that remains valid after the handle is closed;
- unmap vs acquire-view: the view either fails because unmap won, or owns its own
  tensor/backing pins and remains valid after the mapping ID disappears;
- concurrent unmap: exactly one thread removes the mapping.

## Randomized concurrent stress

`tests/randomized_stress_host_test.c` adds longer mixed workloads:

- 8 threads × 25,000 handle install/query/attenuate/revoke/close cycles,
- 8 threads × 4,000 map/view/translate/unmap cycles,
- 4 threads × 8,000 cross-domain SHARE/close cycles.

The mapping workers touch disjoint payload bytes. This is deliberate: Step 9 is
checking kernel metadata synchronization. Synchronization of simultaneous
application writes to the same shared tensor payload remains the responsibility
of the higher-level execution/data-dependency model.

## Security hardening discovered by Step 9

### 1. Generation counters no longer wrap

Earlier Steps used a 24-bit generation field and wrapped from `0xffffff` back to
`1`. After enough reuse, an ancient stale token could theoretically become valid
again (ABA).

Step 9 changes handle and mapping generation behavior:

```text
generation 0x000001 ... 0xffffff
                         |
                         v
                 permanently exhausted slot
                 generation = 0
```

Generation zero is never allocated. A slot that reaches the final representable
generation is retired rather than reused.

This trades an extremely long-lived namespace slot for stronger stale-token
safety. With 64 slots and 24-bit generations, exhaustion requires extreme churn,
but security no longer depends on assuming it never happens.

### 2. Mapping virtual-window arithmetic is overflow checked

A domain mapping window is derived from:

```text
VA_BASE + (domain_id - 1) * DOMAIN_STRIDE
```

Step 9 now validates this arithmetic before constructing the window. Domain IDs
that cannot be represented in the configured virtual-address layout receive no
mapping window rather than wrapping into another address range.

### 3. Mapping-count invariant is observable in tests

`ai_backing_mapping_count()` exposes a read-only atomic count for diagnostics and
invariant tests. It does not mutate lifetime state.

## Sanitizers

The Step 9 suite is run under:

- AddressSanitizer,
- UndefinedBehaviorSanitizer,
- ThreadSanitizer.

ThreadSanitizer is applied to the race matrix and randomized concurrent stress
suite. Legacy Step 8 concurrency tests are also retained.

## Acceptance criteria

Step 9 is complete when all of the following hold:

- forged handles/mapping IDs do not resolve,
- no rights combination can manufacture authority,
- exhausted tables fail closed,
- stale tokens remain stale after reuse,
- generation exhaustion cannot wrap into an old generation,
- refcount/mapping-count arithmetic fails closed at boundaries,
- close/revoke/acquire/unmap race outcomes preserve lifetime invariants,
- randomized concurrent stress leaves all table/refcount registries balanced,
- ASan/UBSan report no memory/undefined-behavior error,
- ThreadSanitizer reports no kernel metadata race in the host harness,
- all earlier Step 3–8 tests remain green.

## Remaining security boundaries

Step 9 does not change the architectural boundaries already documented:

- no hardware-enforced per-domain page tables/user mode yet,
- local token revocation is not transitive lineage revocation,
- shared tensor payload synchronization is not supplied automatically,
- WORK/GENERIC handles still lack managed object lifetime hooks,
- spinlocks are not yet IRQ-save/preemption-aware,
- physical page recycling still awaits a real page-frame allocator/kernel heap.

The next step is **Phase 4 Step 10 — benchmark and integration gate**.
