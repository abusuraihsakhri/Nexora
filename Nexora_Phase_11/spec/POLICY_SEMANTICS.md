# Policy Semantics — Phase 11.1 Compact Specification

Let `G` be the finite set of occupied grants and `E` the current epoch. `ANY_ID` is a policy wildcard sentinel and is outside the domain of concrete request object IDs.

Well-formed request:

```text
valid_request(r) ≜
  r.rights ≠ ∅ ∧
  r.kind ∈ ConcreteResourceKinds ∧
  r.object ≠ ANY_ID ∧
  0 ≤ r.quota ≤ 10000
```

For grant `g` and request `r`:

```text
subject_match(g,r)  ≜ g.subject = r.subject
kind_match(g,r)     ≜ g.kind = ANY ∨ g.kind = r.kind
object_match(g,r)   ≜ g.object = ANY_ID ∨ g.object = r.object
current(g)          ≜ g.active ∧ g.epoch = E
live(g,r)           ≜ g.not_before ≤ r.now ∧
                      (g.expires = 0 ∨ r.now < g.expires)
base_match(g,r)     ≜ subject_match ∧ kind_match ∧ object_match ∧ current ∧ live
rights_cover(g,r)   ≜ (g.rights ∩ r.rights) = r.rights
bytes_cover(g,r)    ≜ g.byte_limit = 0 ∨ r.bytes ≤ g.byte_limit
quota_cover(g,r)    ≜ r.quota ≤ g.quota
```

`bytes_cover` and `quota_cover` are per-operation admission predicates, not cumulative consumption accounting.

Explicit denial:

```text
DeniedExplicit(r) ≜ ∃ g ∈ G :
  base_match(g,r) ∧ g.effect = DENY ∧
  (g.rights ∩ r.rights) ≠ ∅
```

Authorization:

```text
Authorized(r) ≜
  valid_request(r) ∧
  ¬DeniedExplicit(r) ∧
  ∃ g ∈ G :
    base_match(g,r) ∧
    g.effect = ALLOW ∧
    rights_cover(g,r) ∧
    bytes_cover(g,r) ∧
    quota_cover(g,r)
```

Root grant well-formedness requires:

```text
effect(g) ∈ {ALLOW, DENY}
rights(g) ≠ ∅
resource_encoding(g) valid
time_window(g) valid
0 < quota(g) ≤ 10000
```

Delegation defines an attenuation preorder `c ⪯ p`:

```text
rights(c) ⊆ rights(p)
resource(c) ⊆ resource(p)
time(c) ⊆ time(p)
bytes(c) ≤ bytes(p)       for bounded parent
quota(c) ≤ quota(p)
depth(c) = depth(p)+1 ≤ max_depth(p)
epoch(c) = epoch(p)
issuer(c) = subject(p)
```

Required safety invariants:

```text
I1  Every occupied grant has a valid effect and encoding.
I2  Active delegated grants have an active parent.
I3  Every delegated grant satisfies c ⪯ p.
I4  Authorization is false when no allow grant completely covers r.
I5  Explicit deny dominates allow.
I6  ANY_ID cannot be used as a concrete request object.
I7  Revocation is closed over descendants.
I8  Epoch bump leaves no pre-bump grant active and permits stale-slot reuse.
I9  Reclamation never relocates active grants.
I10 Every authorization/delegation/revocation/epoch action is auditable.
```

Management semantics:

```text
revoke(...) and bump_epoch(...) are trusted kernel-management primitives.
actor_subject_id is audit attribution, not an authorization proof.
```

Concurrency semantics:

```text
The engine is externally synchronized.
Authorization must remain valid through privileged resource commitment,
or be revalidated before commitment.
```
