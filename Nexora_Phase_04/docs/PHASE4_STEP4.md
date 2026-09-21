# Nexora Phase 4 — Step 4: Handle access rights and attenuation

## Goal

Add explicit, non-forgeable authority to each domain-scoped object handle.

Step 3 answered **which kernel object does this token name?** Step 4 adds the second security question: **what may the holder do with that object?**

The handle table now stores an independent rights mask beside the object pointer, type, generation, and occupancy state.

## Rights model

The prototype defines six orthogonal rights:

```text
READ      inspect/read object data or metadata
WRITE     mutate object data where the object itself permits mutation
MAP       request a virtual mapping of the backing object
SHARE     retain this handle while delegating a derived handle to another domain
TRANSFER  move authority to another domain instead of retaining it locally
ADMIN     perform future administrative/revocation operations
```

No right implies another right. For example, `ADMIN` does not automatically imply `READ`, and `MAP` does not imply `WRITE`.

`SHARE` and `TRANSFER` become operational in Step 5. `MAP` becomes operational in the zero-copy mapping step. They are represented now so delegation rules can be built against a stable rights model.

## Protected table state

The numeric 64-bit handle remains:

```text
63                              32 31             8 7        0
+----------------------------------+----------------+----------+
|          domain tag (32)         | generation(24) | slot(8)  |
+----------------------------------+----------------+----------+
```

Rights are intentionally **not encoded in this token**.

Each protected table entry contains:

```text
object pointer
generation
object type
rights mask
occupied state
```

This matters because a caller cannot gain authority by flipping bits in a user-visible handle value. It also lets the kernel reduce rights in place without changing object identity.

## Rights-aware resolution

Resolution is now:

```text
(domain, handle, expected_type, required_rights)
    -> object or rejection
```

The kernel checks:

1. domain lifecycle permits existing-handle access;
2. handle is nonzero;
3. embedded domain tag matches;
4. slot is valid and occupied;
5. generation matches;
6. object type matches;
7. every requested right is present in the entry.

Only after all checks succeed does the internal object pointer become available.

A caller may pass `required_rights == 0` only when it needs an identity/type validity check rather than authorization for an operation.

## Rights attenuation

`ai_handle_restrict_rights()` can replace a handle's rights only with a nonzero subset of its current rights.

Example:

```text
initial: READ | WRITE | MAP | SHARE
                |
                v
restrict: READ | MAP | SHARE       accepted
                |
                v
restore WRITE                       rejected
```

This implements a core capability-security property: **authority may be voluntarily reduced but not amplified**.

Restriction does not change the handle generation because the object identity has not changed. Existing copies of the same numeric token immediately observe the reduced rights because authority resides in the kernel table entry.

## Tensor-level immutability

Handle authority cannot override an object's stronger intrinsic policy.

For tensors marked `AI_TENSOR_READONLY`, installing a handle containing `WRITE` is rejected even if the creating domain requests it.

Therefore:

```text
read-only tensor + READ|MAP   -> accepted
read-only tensor + WRITE      -> rejected
```

This is a prototype object-policy check. Later backing objects will enforce immutability and mapping protections at the VM/page-table layer as well.

## Domain lifecycle

The Step 3 lifecycle rules remain:

- new handles can be installed only while a domain is `ACTIVE`;
- existing handles can be resolved, restricted, and closed while the domain is `QUIESCING`;
- a domain cannot be destroyed until its handle table is empty.

## Security invariants introduced in Step 4

- unknown rights bits are rejected;
- zero-authority handles cannot be installed;
- rights are stored only in protected kernel state;
- required rights are checked on resolution;
- rights attenuation is monotonic;
- rights escalation through attenuation is impossible;
- foreign domains still cannot resolve copied tokens;
- stale generations remain invalid;
- read-only tensor policy dominates handle-requested WRITE authority.

## Acceptance tests

The host-side test suite and kernel boot demo exercise:

- valid and invalid rights masks;
- read-only tensor WRITE rejection;
- `READ|MAP` success;
- missing `ADMIN` rejection;
- multi-right resolution;
- cross-domain rejection;
- object-type rejection;
- unknown-right rejection;
- rights inspection;
- monotonic attenuation;
- failed rights escalation;
- stale-handle invalidation;
- generation change after slot reuse;
- quiescing-domain cleanup;
- handle-table capacity and drain.

## Phase boundary

Step 4 defines **authority**, but does not yet create cross-domain derived handles.

Step 5 will implement delegation:

```text
Domain A handle
    + SHARE right
    + requested subset of rights
              |
              v
Domain B receives a new local handle
              |
              v
same kernel tensor/backing object
```

Transfer semantics will similarly require `TRANSFER` and will remove or invalidate the source authority as part of the operation.
