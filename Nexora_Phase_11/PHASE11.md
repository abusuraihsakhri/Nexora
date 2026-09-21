# Nexora — Phase 11.1

## Formal Security, Policy Enforcement, and Hardening Corrections

Phase 11 converts the Phase 10 agent-capability concept into an enforceable, testable policy core. Phase 11.1 is the hardening pass performed after an independent cross-check found edge cases in effect validation, wildcard-sentinel handling, grant-table lifecycle, management-operation trust semantics, and reproducibility of the phase gate.

### Ten implementation gates

1. **Formal security contract** — authorization and delegation semantics are explicit and executable.
2. **Typed resource identity** — tensors, models, devices, endpoints, remote tensors, and collectives use distinct resource kinds.
3. **Deny-by-default evaluator** — no complete matching allow means denial; explicit deny has precedence.
4. **Per-operation quantitative admission** — time, per-request byte demand, and per-request accelerator demand are checked against grant ceilings. These are not cumulative accounting mechanisms.
5. **Attenuating delegation** — child grants cannot add rights, widen scope, extend time, increase byte ceilings, or increase quota ceilings.
6. **Revocation and lifecycle** — targeted revocation cascades to descendants; epoch bump globally invalidates prior grants; inactive slots are reclaimable without relocating active grants.
7. **Audit ring** — authorization, delegation, revocation, and epoch changes receive ordered reason-coded records.
8. **Kernel integration contract** — the policy module is freestanding and documents management-call trust, external synchronization, and authorization/commit TOCTOU requirements.
9. **Adversarial host tests** — escalation, wildcard widening, wildcard-sentinel request misuse, invalid effects, quota bypass, expiry, deny override, revocation, table exhaustion, reclamation, and audit wrapping are tested.
10. **Reproducible phase gate** — deterministic/property tests, ASan/UBSan, GCC static analysis, freestanding compile, clean kernel link, dependency checks, source regression checks, and package checksums run from one script.

### Phase 11.1 corrections

The hardening pass specifically fixes:

- invalid `ai_policy_effect` values being accepted by grant creation and invariant validation;
- `AI_POLICY_ANY_ID` being usable as a concrete request object ID;
- permanent grant-table exhaustion after revocation or epoch invalidation;
- ambiguity about `actor_subject_id` on revoke/epoch operations;
- ambiguity between per-request ceilings and cumulative byte/GPU accounting;
- undocumented SMP/concurrency and authorization-to-operation TOCTOU requirements;
- the previous phase gate not actually rerunning sanitizer/static-analysis checks itself.

### Exit criteria

Phase 11.1 is complete when:

- deterministic and property tests pass under `-Wall -Wextra -Werror`;
- ASan + UBSan host tests pass;
- GCC `-fanalyzer` passes for the policy core;
- `policy.c` compiles freestanding;
- the clean x86-64 kernel ELF links with the module;
- no unexpected libc dependency is introduced;
- structural policy invariants pass;
- no tested delegation increases authority;
- revocation removes descendant authority;
- inactive grant slots can be reused under table pressure;
- wildcard sentinels cannot be submitted as concrete request object IDs;
- audit retrieval remains ordered after wraparound;
- release-package checksums verify.

### Remaining boundary

Phase 11.1 is still a **logical policy core**, not complete hardware/resource isolation. Real enforcement points must provide trusted subject/resource identities and trusted byte/quota demand, externally synchronize the engine on SMP systems, and prevent authorization/commit races. Cumulative bandwidth/byte budgets, hardware GPU partitioning, IOMMU isolation, persistent sealed audit, distributed identity, and cryptographic capability integrity remain later work.
