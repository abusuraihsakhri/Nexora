# Phase 12 Integration Guide

This package is an additive subsystem intended to be merged into the current Nexora tree.

## 1. Add the files

```text
include/ai/reliability.h
src/ai/reliability.c
```

If the current tree already provides `include/kernel/types.h`, keep the existing file. The copy in this package is only present so the Phase 12 tests are self-contained.

## 2. Add the object to the kernel build

Add:

```text
src/ai/reliability.c
```

to the existing kernel C source/object list.

The source compiles freestanding and has no libc dependency.

## 3. Initialize after the monotonic clock is available

Example:

```c
static ai_reliability g_reliability;

void ai_runtime_init(u64 now_ns) {
    ai_rel_policy policy = ai_rel_default_policy();
    ai_rel_init(&g_reliability, &policy, now_ns);
}
```

Do not fabricate time inside the reliability layer. Pass the kernel monotonic clock value explicitly.

## 4. Register containment domains

Examples:

```c
ai_rel_domain *gpu0 = ai_rel_domain_register(
    &g_reliability,
    AI_REL_DOMAIN_DEVICE,
    gpu0_device_id,
    now_ns
);

ai_rel_domain *agent = ai_rel_domain_register(
    &g_reliability,
    AI_REL_DOMAIN_AGENT,
    agent_id,
    now_ns
);

ai_rel_domain *remote = ai_rel_domain_register(
    &g_reliability,
    AI_REL_DOMAIN_REMOTE_NODE,
    remote_node_id,
    now_ns
);
```

Store the returned reliability-domain ID with the resource/agent descriptor.

## 5. Hook the scheduler journal

At minimum, record:

```c
ai_rel_journal_append(&g_reliability,
    AI_REL_EVENT_SCHED_PICK,
    agent_id,
    work_id,
    scheduler_id,
    priority,
    now_ns);

ai_rel_journal_append(&g_reliability,
    AI_REL_EVENT_RESOURCE_ASSIGN,
    agent_id,
    work_id,
    device_id,
    tensor_locality_cost,
    now_ns);
```

These records allow a host-side simulator to compare two scheduling runs without embedding logging strings into the kernel path.

## 6. Gate admission after authorization

Use:

```c
if (!ai_rel_can_admit(&g_reliability, resource_rel_domain_id, critical, now_ns)) {
    /* choose another resource or reject/defer */
}
```

Authorization must still happen first. Reliability admission is not a capability check.

## 7. Heartbeats

For remote nodes/devices/services that expose liveness signals:

```c
ai_rel_heartbeat(&g_reliability, domain_id, now_ns);
```

A periodic maintenance tick can call:

```c
ai_rel_check_heartbeats(&g_reliability, now_ns);
ai_rel_release_expired_quarantines(&g_reliability, now_ns);
```

The tick interval should be shorter than the heartbeat timeout but need not be exact.

## 8. Fault reporting

Subsystem owners translate concrete errors into Phase 12 fault codes:

```c
ai_rel_record_fault(
    &g_reliability,
    domain_id,
    AI_REL_SEV_ERROR,
    AI_REL_FAULT_DMA_ERROR,
    now_ns
);
```

Then obtain the deterministic policy decision:

```c
ai_rel_recovery_action action = ai_rel_choose_recovery(
    &g_reliability,
    domain_id,
    AI_REL_FAULT_DMA_ERROR
);

ai_rel_begin_recovery(&g_reliability, domain_id, action, now_ns);
```

The device/runtime/network subsystem executes the chosen action and reports success/failure.

## 9. Checkpoint boundary

When the work-graph layer has committed a restorable checkpoint:

```c
ai_rel_checkpoint cp = ai_rel_checkpoint_capture(
    &g_reliability,
    graph_id,
    graph_epoch,
    scheduler.dispatch_count,
    completed_work_count,
    now_ns
);
```

Store `cp` next to the actual graph/tensor checkpoint metadata. Phase 12 intentionally does not copy tensor data.

## 10. Safe-mode integration

When `ai_rel_safe_mode()` is true:

- stop admitting non-critical work;
- keep recovery/control work schedulable;
- avoid new remote placements unless explicitly necessary for recovery;
- preserve existing security restrictions;
- expose the state through serial/debug diagnostics.

## Required integration tests in the full kernel

1. Simulated GPU DMA error triggers degradation, retry, then reset/quarantine after retry budget.
2. Remote-node heartbeat loss removes the node from placement.
3. Capability violation does not enter an automatic retry loop.
4. Fatal work-graph invariant marks the graph domain failed and enters safe mode.
5. Scheduler journal contains identical records for repeated deterministic simulations.
6. Journal wrap increments `dropped_events` and never corrupts memory.
7. Quarantine expiry does not automatically mark a domain healthy; it returns only to degraded state.
8. Recovery success increments generation and clears transient failure counters.
