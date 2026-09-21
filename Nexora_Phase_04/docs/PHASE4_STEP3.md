# Nexora Phase 4 — Step 3: Per-domain opaque handle tables

## Goal

Replace externally visible kernel-object pointers with generation-protected 64-bit handles that are meaningful only inside one work domain.

This step deliberately does **not** implement per-handle READ/WRITE/MAP/SHARE rights; those belong to Step 4. Step 3 establishes the namespace, stale-handle protection, type safety, and lifecycle rules that Step 4 will build on.

## Handle format

The prototype handle is encoded as:

```text
63                              32 31             8 7        0
+----------------------------------+----------------+----------+
|          domain tag (32)         | generation(24) | slot(8)  |
+----------------------------------+----------------+----------+
```

The low 8-bit slot token stores `slot_index + 1`; zero therefore remains invalid.

Current limits:

- 64 simultaneous handles per domain;
- 32-bit domain identity in the handle token;
- 24-bit per-slot generation counter.

The work-domain registry refuses IDs that cannot be represented by the handle encoding.

## Resolution invariant

Resolution is always contextual:

```text
(domain, handle, expected_object_type) -> object or rejection
```

The kernel verifies, in order:

1. the domain is live (`ACTIVE` or `QUIESCING`);
2. the handle is nonzero;
3. the embedded domain tag matches the resolving domain;
4. the slot is in range;
5. the slot is occupied;
6. the generation matches;
7. the object type matches the caller's expectation.

Only then is the kernel pointer returned internally.

## Stale-handle defense

Closing a handle clears the object pointer and increments the slot generation. If the same table slot is later reused, the new handle differs from the old handle.

Therefore:

```text
install -> H(gen=1)
close H
reinstall -> H'(gen=2)
resolve old H -> rejected
resolve H' -> object
```

Generation zero is skipped. A 24-bit generation eventually wraps, so this is strong prototype stale-handle protection rather than a formal infinite-lifetime guarantee. Production designs should pair generations with stronger namespace epochs or non-repeating object IDs.

## Cross-domain behavior

A numeric handle copied from Domain A into Domain B is rejected before B's handle table is consulted because the embedded domain identity does not match.

This is stronger than merely using independent per-domain slot arrays, where coincidentally identical slot/generation values could otherwise resolve to an unrelated local object.

## Domain lifecycle integration

A domain cannot be destroyed while `handles.live_count != 0`.

New handles may only be installed while a domain is `ACTIVE`. Existing handles may still be resolved and closed while a domain is `QUIESCING`, which permits orderly teardown.

## Object typing

Entries carry an object class:

- `TENSOR`
- `BACKING`
- `WORK`
- `GENERIC`

A caller resolving a tensor handle as a work-node handle is rejected. Step 4 will add rights checks on top of this type check.

## Boot-time acceptance test

The kernel now verifies during boot that:

- a tensor handle can be installed and resolved by its owner;
- another domain cannot resolve it;
- a type-confused resolution is rejected;
- a domain with a live handle cannot be destroyed;
- closing invalidates the token;
- slot reuse increments the generation;
- the old generation remains invalid;
- live-handle accounting returns to zero;
- the domain can then quiesce and be destroyed.

Any failed assertion panics the kernel.

## Phase boundary

Step 3 provides object identity and namespace safety. It does not yet answer **what operations a valid holder may perform**. That is Step 4: per-handle access rights and attenuation.
