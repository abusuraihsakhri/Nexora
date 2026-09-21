# Phase 10 Integration Guide

## 1. Add source files
Add `kernel/phase10/*.c` to the kernel build and expose `include/nexora/phase10` on the internal include path.

## 2. Instantiate global/system objects
Create one system-level health registry, checkpoint store, audit ring and recovery engine. Per-tenant/per-job stores can be introduced later when isolation domains require separate retention policies.

## 3. Bind platform recovery mechanisms
Implement an executor matching:

```c
nx_p10_status_t executor(nx_recovery_action_t action,
                         nx_p10_id_t subject_id,
                         uint64_t checkpoint_id,
                         void *opaque);
```

The executor should route actions to existing Nexora mechanisms:
- RETRY: task/runtime retry
- RESTART: service/task/device restart
- ROLLBACK: restore checkpoint and generation
- MIGRATE: hand off to Phase 9 placement/transport
- QUARANTINE: optional platform-side containment in addition to Phase 10's local health-state quarantine
- PANIC: enter kernel fatal path

RETRY/RESTART/ROLLBACK/MIGRATE/PANIC fail with `NX_P10_ESTATE` if no executor is bound. This is intentional fail-closed behavior.

## 4. Add heartbeats
Heartbeat at meaningful progress boundaries rather than every loop iteration. For accelerators, distinguish transport liveness from job progress.

## 5. Configure policy units
`nx_p10_time_t` is unit-neutral. Bind it consistently to the existing monotonic Nexora timebase (ticks, ns, us, etc.). Treat zero time thresholds as disabled.

## 6. Protect shared structures
Before enabling on SMP, wrap mutations using the kernel's own spinlock/RCU primitives. This package avoids choosing a synchronization primitive because the exact primitive must match the earlier Nexora core implementation.

## 7. Persist checkpoints
`nx_checkpoint_store_t` stores metadata only. Payload storage belongs to the memory/runtime/distributed checkpoint subsystem. The `payload_tag` should reference or identify the actual snapshot.

A saturated metadata store can recycle the oldest committed record, but it will not evict a `PREPARED` record. If every slot is prepared, `nx_checkpoint_prepare()` returns `NX_P10_ENOSPC`; the caller must resolve/abort old preparations or increase capacity.

## 8. Secure integrity
The built-in metadata integrity tag detects accidental corruption only. If hostile tampering is in scope, bind checkpoint metadata to the system's authenticated integrity/attestation mechanism.

## 9. Preserve action-derived authorization
Do not treat `nx_recovery_decision_t.required_capability` as an authorization token. It is informational. `nx_recovery_execute()` independently derives the required capability from `action` and checks the supplied security context.

## 10. Audit integration
The standalone recovery engine writes recovery decision/result and security-denial events. If the global Nexora telemetry contract requires health-transition, checkpoint-lifecycle or node-selection events, emit those from the integrated authoritative hooks where actor identity, monotonic time and locking are already defined.
