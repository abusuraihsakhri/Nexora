# Nexora Phase 4 — Step 2: Work Domains

## Goal

Introduce a kernel-level isolation and ownership unit for AI workloads before tensor handles are added.

A work domain is not yet a hardware-enforced userspace process. In Phase 4 it is a logical kernel object that provides:

- a stable 64-bit domain ID;
- lifecycle state;
- resource ownership/accounting;
- resource limits;
- a lookup boundary for the handle table added in Step 3.

## State machine

```text
NEW -> ACTIVE -> QUIESCING -> DEAD
```

`ACTIVE -> DEAD` is intentionally rejected. A live domain must first quiesce, release its attributed resources, then retire.

## Resource limits

Each domain currently tracks:

- memory bytes;
- number of tensors;
- number of work nodes.

A zero limit means unlimited in the prototype.

The accounting API rejects operations when a quota would be exceeded and detects accounting underflow.

## Registry invariants

1. Domain ID 0 is always invalid.
2. IDs monotonically increase and are not immediately reused.
3. Lookup never returns a DEAD domain.
4. A domain cannot be destroyed while memory, tensors, or work nodes remain attributed to it.
5. The early bump allocator cannot reclaim retired domain structures yet; destruction is logical until a real allocator is introduced.

## Why this precedes handle tables

Step 3 requires every handle to belong to exactly one domain. A handle must never be globally valid. Resolution will therefore have the form:

```text
(domain_id, handle) -> per-domain handle table -> object
```

rather than:

```text
handle -> global object
```

This prevents one domain from resolving another domain's numeric handle by accident or by forgery.

## Phase 4 boundaries

This step does not claim hostile-process isolation. CPU privilege separation, syscall entry, address-space isolation, and userspace execution belong to later phases.
