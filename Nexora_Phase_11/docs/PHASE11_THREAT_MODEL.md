# Phase 11.1 Threat Model

## Assets

The policy layer protects logical authority over tensors, models, accelerator devices, network endpoints, remote tensors, and collective operations.

Protected dimensions include operation rights, resource identity, time window, per-operation byte demand, per-operation accelerator demand, delegation depth, and revocation epoch.

## Adversary model

An untrusted agent or user-mode workload may submit arbitrary requests and attempt to:

1. access an ungranted resource;
2. request rights not granted;
3. exceed per-operation byte or device-quota ceilings;
4. reuse an expired or revoked grant;
5. delegate additional rights;
6. widen exact resource scope to a wildcard;
7. submit `AI_POLICY_ANY_ID` as if it were a concrete resource;
8. extend delegated lifetime or quantitative ceilings;
9. bypass a deny rule with a separate allow rule;
10. retain descendant authority after parent revocation;
11. exhaust the grant table with historical revoked entries;
12. exploit malformed/unknown policy-effect values.

The policy engine is designed to reject or safely handle these cases under its trust assumptions.

## Trusted computing base

Phase 11.1 trusts:

- kernel memory integrity;
- the policy-engine implementation;
- authenticated management wrappers around revoke/epoch operations;
- external synchronization around the policy engine on concurrent/SMP systems;
- correct subject/resource identity supplied by enforcement points;
- the time source supplied to authorization;
- trusted derivation of per-operation byte and accelerator demand;
- correct coupling between an authorization result and the privileged operation that follows it.

If an attacker can overwrite kernel memory, forge kernel-side identities, invoke management primitives without an authorized wrapper, race authorization against resource commitment, or lie about demand/accounting inputs, this logical policy layer alone does not provide security.

## Security properties

### P1 — Default denial

No matching complete allow grant implies denial.

### P2 — Explicit-deny precedence

A matching active deny that overlaps any requested right blocks the request even when an allow also matches.

### P3 — Complete-rights coverage

One allow grant must contain every requested right. Rights are not unioned across grants.

### P4 — Request validity

Wildcard sentinels and invalid resource/effect encodings cannot be used as valid concrete requests/grants.

### P5 — Per-operation constraint monotonicity

Authorization never permits a declared byte or accelerator demand above the matching grant ceiling. This is admission checking, not cumulative metering.

### P6 — Delegation attenuation

Delegation can only preserve or reduce authority.

### P7 — Revocation closure

Revoking a grant revokes every descendant grant.

### P8 — Epoch invalidation

A global epoch transition invalidates all existing grants and releases their storage for reuse.

### P9 — Storage recovery

Inactive historical grants cannot permanently consume all grant slots; inactive slots are reclaimable without moving active grants.

### P10 — Decision observability

Every policy decision generates an audit record with an explicit reason/action attribution.

## Residual risks

Important risks intentionally deferred or delegated to integration:

- authorization-to-execution TOCTOU unless the caller follows the synchronization contract;
- DMA isolation and IOMMU enforcement;
- side channels across shared accelerators;
- cumulative bandwidth/byte accounting;
- real GPU utilization measurement and reservation enforcement;
- network endpoint authenticity;
- cryptographic identity of remote resources;
- distributed revocation convergence;
- audit persistence, anti-rollback, and overflow archival;
- denial of service through **active** grant-table exhaustion;
- starvation/DoS through repeated expensive policy calls.
