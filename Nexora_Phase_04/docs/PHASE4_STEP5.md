# Nexora Phase 4 — Step 5: Cross-domain handle sharing and transfer

## Goal

Make the `SHARE` and `TRANSFER` rights introduced in Step 4 operational.

Step 5 adds controlled delegation between isolated Nexora work domains without exposing kernel pointers or making a copied source-domain token usable in another domain.

The central rule is:

```text
same kernel object
      |
      +-- Domain A local handle
      +-- Domain B local handle
```

The two domains receive different opaque tokens because each token is bound to its own domain namespace.

## New APIs

```c
ai_handle_t ai_handle_share(
    ai_domain *source_domain,
    ai_handle_t source_handle,
    ai_domain *target_domain,
    ai_handle_rights_t requested_rights
);

ai_handle_t ai_handle_transfer(
    ai_domain *source_domain,
    ai_handle_t source_handle,
    ai_domain *target_domain,
    ai_handle_rights_t requested_rights
);
```

Both functions return `AI_HANDLE_INVALID` on failure.

## SHARE semantics

`ai_handle_share()` derives a new capability in another active domain while retaining the source handle.

Preconditions:

1. source and target domains are distinct;
2. both domains are `ACTIVE`;
3. the source token resolves in the source domain;
4. the source handle contains `SHARE`;
5. `requested_rights` is nonzero and valid;
6. every requested right is already present on the source handle;
7. the target handle table has free capacity;
8. the object-level policy accepts the delegated rights.

On success:

```text
Domain A
  H_A: READ|WRITE|MAP|SHARE|TRANSFER
             |
             | share READ|MAP
             v
Domain B
  H_B: READ|MAP
```

`H_A` and `H_B` resolve to the same kernel object, but `H_A != H_B` and the embedded domain tags differ.

Closing `H_B` does not affect `H_A`.

## TRANSFER semantics

`ai_handle_transfer()` moves authority instead of duplicating it.

The source must contain `TRANSFER`; `SHARE` is not required.

A transfer is rights-attenuating. For example:

```text
source:      READ|WRITE|TRANSFER
requested:   READ
result:      destination gets READ
             source becomes stale
             WRITE authority is discarded
```

The implementation uses destination-first commit order:

```text
1. validate source and requested rights
2. install destination-local handle
3. close/invalidate source handle
4. if source close unexpectedly fails, close destination as rollback
```

This ordering avoids a transient state in which the operation has invalidated the source but failed to create the destination.

The current kernel is still a single-core prototype. Step 8 will place this sequence under synchronization and make the operation race-safe under concurrent CPUs.

## Rights attenuation and non-amplification

Delegation never creates authority that the source did not possess.

```text
source rights = READ|MAP|SHARE

share READ                  -> allowed
share READ|MAP              -> allowed
share READ|WRITE            -> rejected
share ADMIN                 -> rejected
```

The delegation-control right itself is checked separately:

- `SHARE` is required to derive another live handle while retaining the source;
- `TRANSFER` is required to move a handle and invalidate the source.

A source with `SHARE` but not `TRANSFER` cannot transfer. A source with `TRANSFER` but not `SHARE` cannot duplicate authority.

## Transitive delegation

A recipient may delegate again only if the delegated rights explicitly include the necessary delegation right.

Example:

```text
A: READ|MAP|SHARE
       |
       | share READ|SHARE
       v
B: READ|SHARE
       |
       | share READ
       v
C: READ
```

C cannot re-share because its handle does not contain `SHARE`.

This gives the kernel a capability-style delegation chain with monotonic authority reduction.

## Domain lifecycle rule

Delegation creates new authority and is therefore allowed only between `ACTIVE` domains.

A `QUIESCING` domain may still resolve, attenuate, and close existing handles so that it can drain cleanly, but it cannot:

- originate a new share;
- originate a transfer;
- receive a new shared handle;
- receive a transferred handle.

This prevents shutdown from creating fresh cross-domain dependencies.

## Failure atomicity in the current prototype

For `SHARE`, any failure simply produces no target handle and leaves the source unchanged.

For `TRANSFER`, failure before destination installation leaves the source unchanged. If destination installation succeeds but source invalidation unexpectedly fails, the destination handle is explicitly closed as rollback.

A particularly important tested case is a full target handle table:

```text
target table full
      |
      v
transfer fails
      |
      +-- source remains valid
      +-- source rights unchanged
      +-- target gains no handle
```

## What Step 5 does not claim yet

Step 5 delegates **object authority**, not physical page mappings.

The current tensor object remains metadata allocated in kernel memory. Step 5 proves that multiple domains can safely hold different local capabilities to the same kernel object. It does not yet provide:

- a page-backed tensor storage object;
- shared virtual mappings;
- MMU-enforced read-only/write mappings;
- zero-copy userspace access;
- full object/backing reference counting;
- concurrent delegation locking;
- revocation trees.

Those are subsequent Phase 4 steps.

## Security invariants added in Step 5

- copied source-domain tokens remain unusable in other domains;
- only the kernel can mint a target-domain token;
- sharing requires `SHARE`;
- transfer requires `TRANSFER`;
- delegated rights must be a subset of source rights;
- zero-right and unknown-right delegations are rejected;
- same-domain delegation is rejected in the prototype;
- quiescing domains cannot create or receive new delegation;
- successful transfer invalidates the source token;
- failed transfer preserves the source token;
- closing a shared recipient handle does not invalidate the source;
- transitive delegation is possible only when the corresponding right was explicitly delegated.

## Acceptance tests

The Step 5 host test suite checks:

- successful cross-domain share;
- same object resolution from recipient-local token;
- recipient rights attenuation;
- source authority preservation after share;
- copied foreign-token rejection;
- transitive share when `SHARE` is delegated;
- inability to re-share without `SHARE`;
- requested-right escalation rejection;
- zero-right rejection;
- same-domain delegation rejection;
- transfer success and source invalidation;
- transfer-time rights attenuation;
- missing `TRANSFER` rejection;
- quiescing source rejection;
- quiescing target rejection;
- full-target transfer failure with source preservation;
- independent recipient close behavior;
- domain destruction after all delegated handles drain.

## Next step

**Phase 4 Step 6 — zero-copy mapping layer.**

Step 6 will attach handles to page-backed tensor storage and map the same physical pages into multiple domain address spaces with permissions derived from `READ`, `WRITE`, and `MAP`.
