# Nexora Phase 11.1 — Policy Enforcement Security Model

## Objective

Phase 10 introduced per-agent capability domains. Phase 11 turned those declarations into a deterministic policy core. Phase 11.1 hardens that core around malformed effects, wildcard-sentinel misuse, grant lifecycle/reclamation, management-call trust, and concurrency assumptions.

The principal rule remains **deny by default**. An operation is authorized only when at least one active allow grant covers the complete request and no active explicit-deny grant overlaps any requested right.

## Security objects

A grant is modeled as:

```text
g = (
  id, parent,
  subject, issuer,
  effect,
  resource_kind, resource_id,
  rights,
  not_before, expires_at,
  byte_limit,
  device_quota_bps,
  delegation_depth, max_delegation_depth,
  epoch, active
)
```

A request is:

```text
r = (
  subject,
  resource_kind, resource_id,
  rights,
  now,
  bytes,
  device_quota_bps
)
```

`device_quota_bps` is expressed in basis points: 10,000 = 100%, 2,000 = 20%.

`AI_POLICY_ANY_ID` is reserved for policy wildcard grants/delegations. It is **not** a valid concrete request object identifier. Real resource allocators must therefore never assign the sentinel value as an object ID.

## Quantitative semantics

The current `byte_limit` and `device_quota_bps` fields are **per-operation admission ceilings**:

```text
r.bytes <= g.byte_limit                  when g.byte_limit != 0
r.device_quota_bps <= g.device_quota_bps
```

They are not cumulative lifetime budgets and they do not measure actual accelerator utilization. A caller that submits a false low demand can defeat the intended quantitative policy. Enforcement points must derive these values from trusted kernel/scheduler state.

Cumulative byte budgets, token buckets, wall-clock GPU accounting, and scheduler-enforced reservation consumption belong in later enforcement/accounting work.

## Authorization semantics

For request `r`, a grant `g` is a candidate when:

1. `g.subject == r.subject`;
2. `g.resource_kind == r.resource_kind` or `g.resource_kind == ANY`;
3. `g.resource_id == r.resource_id` or `g.resource_id == ANY_ID`;
4. `g.active == true`;
5. `g.epoch == current_epoch`;
6. `g.not_before <= r.now`;
7. `g.expires_at == 0` or `r.now < g.expires_at`.

A request is malformed and denied when it has zero rights, uses resource kind `ANY`, uses `AI_POLICY_ANY_ID` as a concrete object ID, uses an invalid resource kind, or requests quota above 10,000 basis points.

An explicit deny candidate blocks the request if its rights overlap any requested right.

An allow candidate authorizes only if:

```text
r.rights ⊆ g.rights
r.bytes <= g.byte_limit        when g.byte_limit != 0
r.device_quota_bps <= g.device_quota_bps
```

No partial-rights composition occurs across multiple allow grants. One grant must cover the whole request.

## Grant well-formedness

A root grant is accepted only when:

- `effect` is exactly `ALLOW` or `DENY`;
- rights are nonzero;
- resource encoding is valid;
- time window is valid;
- quota is in `(0, 10000]`.

`ai_policy_validate()` independently checks effect validity so memory corruption or direct struct mutation cannot silently pass the invariant gate.

## Delegation attenuation

A delegated grant must be no more powerful than its parent. Child `c` must satisfy:

```text
c.rights ⊆ p.rights
resource(c) ⊆ resource(p)
time_window(c) ⊆ time_window(p)
byte_limit(c) <= byte_limit(p)          if p is bounded
quota(c) <= quota(p)
depth(c) = depth(p) + 1 <= max_depth(p)
epoch(c) = epoch(p)
issuer(c) = subject(p)
```

Wildcard resources can be narrowed to exact resources; exact resources cannot be widened to wildcards.

## Revocation and grant-slot lifecycle

Targeted revocation is cascading. Revoking a parent grant revokes every active descendant, preserving the invariant that an active delegated grant never depends on an inactive parent.

Inactive grants are tombstones until reclaimed. `ai_policy_reclaim_inactive()` converts inactive slots into reusable holes without moving active grants. Allocation can trigger reclamation when storage is otherwise full. This avoids permanent exhaustion from historical revoked entries while preserving active-grant pointer stability.

An epoch bump is a global revocation barrier. Every existing grant is marked inactive, stale slots are reclaimed, then the epoch advances. New grants are issued only in the new epoch.

Pointers returned by grant-creation APIs are not ownership handles. A pointer to an inactive/reclaimed grant may cease to describe the historical grant; callers should retain grant IDs when they need stable identity.

## Management-operation trust contract

`ai_policy_revoke()` and `ai_policy_bump_epoch()` are **trusted kernel-management primitives**. Their `actor_subject_id` argument is audit attribution, not an authorization check.

Therefore a syscall, control-plane command, or agent-facing API must authenticate/authorize management authority **before** invoking either primitive. Direct exposure to an untrusted caller would be a security error.

## Concurrency and TOCTOU contract

`ai_policy_engine` has no internal lock or atomic protocol. SMP integration must serialize reads/mutations externally.

For privileged operations, a mere pattern of:

```text
authorize();
unlock();
execute_resource_operation();
```

can race revocation or an epoch transition. Integration must either hold a suitable policy/resource lock through authorization and commitment, or revalidate a policy generation/epoch immediately before the irreversible privileged action.

## Audit trail

Every authorization decision, delegation attempt, revocation, and epoch bump is written to a fixed-size ring buffer with:

- monotonic audit sequence;
- action type;
- subject/actor attribution;
- resource;
- rights;
- decision;
- reason code;
- matched grant;
- policy epoch.

The ring is in-memory and overwrites old entries after capacity is reached. Persistence, cryptographic sealing, and remote attestation are later phases.

## Executable invariants

`ai_policy_validate()` checks occupied grant slots for:

1. unique nonzero IDs;
2. effect in `{ALLOW, DENY}`;
3. nonzero rights;
4. valid wildcard/resource encoding;
5. well-formed time windows;
6. quotas in `(0, 10000]`;
7. bounded delegation depth;
8. existing parent grants for delegated entries;
9. allow-only delegation chains;
10. no active child with inactive parent;
11. issuer identity preservation;
12. no rights amplification;
13. no resource widening;
14. no time widening;
15. no byte-ceiling widening;
16. no quota widening;
17. parent/child epoch equality.

Free/reclaimed slots (`id == 0`, inactive) are ignored.

## Non-goals in Phase 11.1

This phase does **not** claim:

- cryptographic unforgeability of in-memory kernel structs;
- secure boot or measured boot;
- trusted execution environments;
- persisted tamper-proof audit logs;
- side-channel resistance;
- hardware-enforced GPU isolation;
- cumulative byte or accelerator accounting;
- verified device drivers;
- distributed identity/revocation consistency;
- theorem-proved kernel correctness.
