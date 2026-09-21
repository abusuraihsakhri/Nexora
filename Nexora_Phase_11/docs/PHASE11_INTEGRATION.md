# Integrating Phase 11.1 into the Phase 10 Tree

Phase 11.1 is intentionally narrow. The core depends only on:

```text
include/kernel/types.h
include/ai/capability.h
```

Required files:

```text
include/ai/policy.h
src/ai/policy.c
```

Optional kernel demonstration:

```text
include/ai/policy_demo.h
src/ai/policy_demo.c
```

## Build integration

Add `src/ai/policy.c` to the kernel C source list. The demo source is optional.

## Mapping Phase 10 capability domains

Typical Phase 10 declarations map as follows:

```text
agent A: tensor X read
  subject_id        = A
  resource_kind     = TENSOR
  resource_id       = X
  rights            = AI_CAP_TENSOR_READ

agent A: model M execute
  resource_kind     = MODEL
  resource_id       = M
  rights            = AI_CAP_MODEL_USE | AI_CAP_EXECUTE

agent A: GPU 0 <= 20% demand
  resource_kind     = DEVICE
  resource_id       = 0
  rights            = AI_CAP_GPU_USE
  device_quota_bps  = 2000

agent B: network destination whitelist
  one ALLOW grant per destination, or a broad wildcard allow plus explicit DENY rules
```

`AI_POLICY_ANY_ID` is reserved for grant/delegation wildcard scope. Never allocate it as a real resource identifier and never submit it in a concrete request.

## Enforcement rule

Do not merely attach grants to an agent object. Privileged resource paths must call the policy engine immediately before the operation.

Bad:

```text
agent.has_gpu_capability == true -> execute
```

Required conceptually:

```text
lock(policy_and_resource_state)
construct kernel-derived request
result = ai_policy_authorize(engine, request)
if !result.allowed:
    unlock
    reject
commit/reserve privileged resource operation
unlock
execute committed operation
```

Equivalent generation/epoch revalidation can be used instead of one coarse lock, but the authorization decision must not become stale before commitment.

## Trusted inputs

The following should be kernel-derived or independently validated, not copied blindly from user payloads:

- subject ID;
- concrete resource ID;
- requested rights;
- operation byte count;
- accelerator-demand estimate/reservation;
- authorization timestamp.

The current `byte_limit` and `device_quota_bps` are **per-operation ceilings**. They do not maintain cumulative consumption. A scheduler/accounting layer must separately enforce cumulative resource budgets if required.

## Management operations

`ai_policy_revoke()` and `ai_policy_bump_epoch()` assume their caller is already trusted and authorized. `actor_subject_id` is only written into the audit event.

Therefore expose them only behind an authenticated kernel/control-plane management path, for example:

```text
if !management_authorized(caller): reject
ai_policy_revoke(...)
```

Do not export these raw functions directly as unprivileged syscalls.

## SMP/concurrency

The policy engine has no internal locking. Every integration must define an external synchronization rule covering:

- grant creation/delegation;
- authorization scans;
- revoke/epoch mutation;
- inactive-slot reclamation;
- the authorization-to-resource-commit interval.

Active grants are not relocated by reclamation. Nevertheless, callers should use stable grant IDs rather than retaining raw pointers across policy mutations, especially after a grant is revoked.
