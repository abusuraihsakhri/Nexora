# Nexora Phase 10 — Resilience, Recovery and Production Hardening

## Objective

Phase 10 converts the Phase 1–9 kernel/runtime stack from a feature-complete prototype into a system that can detect failures, contain blast radius, select recovery actions, protect privileged recovery operations, restore from known-good checkpoints, and make distributed recovery decisions.

This layer is intentionally policy/mechanism separated. Nexora's platform-specific restart, rollback, migration and panic mechanisms are injected through `nx_recovery_executor_fn` rather than hard-coded here. Executor-dependent actions fail closed when that mechanism is absent.

## Subsystems

### 1. Health registry
Tracks kernel/runtime/network/accelerator/distributed components with heartbeat age, failure counters, generation numbers and explicit health state.

State model:
`UNKNOWN -> HEALTHY -> DEGRADED -> FAILED -> RECOVERING -> HEALTHY`

An independent terminal containment path exists:
`FAILED -> QUARANTINED`

Heartbeat-driven state transitions and explicit state changes advance the health generation counter.

### 2. Watchdog
Runs periodic health scans and converts liveness failures into recovery decisions. It does not itself know how to reset a device or migrate a distributed job; those remain platform hooks.

### 3. Recovery policy engine
Failure classes are mapped to increasingly strong responses:
- transient failure -> retry/restart
- process/service failure -> restart/rollback/quarantine
- accelerator/node failure -> migrate
- memory corruption -> rollback/restart
- integrity failure -> quarantine
- kernel-critical failure -> panic

Attempt counters plus `retry_limit`, `restart_limit`, `rollback_after` and `quarantine_after` provide bounded escalation instead of unbounded restart loops.

Authorization is derived from the action inside `nx_recovery_execute()` rather than trusting decision metadata supplied by a caller.

### 4. Checkpoint metadata
Maintains fixed-capacity checkpoint metadata with prepare/commit/invalidate lifecycle, owner/generation ordering, and an integrity tag. A prepared record is never silently evicted; under saturation, committed records may be recycled while an all-prepared store returns `NX_P10_ENOSPC`.

The included integrity tag is a corruption detector, **not a cryptographic MAC**. Production secure-boot/attestation paths should replace or wrap it with authenticated integrity provided by the platform security layer.

Rollback and checkpoint-assisted migration validate that a referenced checkpoint is committed, integrity-valid, present, and owned by the subject being recovered.

### 5. Capability-gated recovery
Privileged actions require explicit capabilities. A recovery decision can exist without being executable by the current principal. Security denials are auditable. Recovery execution derives the required capability from the action itself.

### 6. Audit ring
Fixed-capacity event log with kernel-friendly snapshot semantics. The standalone recovery engine emits:
- recovery decision
- recovery result
- security denial

Identifiers for health-transition, checkpoint-lifecycle and node-selection events are reserved for authoritative integrated hooks. The standalone health/checkpoint/node modules do not claim to emit those events automatically.

### 7. Distributed recovery target selection
Phase 10 consumes Phase 9-style node health data and chooses a reachable, non-quarantined recovery target using a deterministic weighted score over load, memory pressure, accelerator pressure and link cost. Equal scores are resolved by lower `node_id`.

## Integration points with prior phases

1. **Scheduler / task layer:** register schedulable services and AI jobs as health subjects; invoke restart/rollback mechanisms from the recovery executor.
2. **Memory layer:** create checkpoints only after memory snapshot completion and mark payload identity in `payload_tag`.
3. **Accelerator layer:** report device heartbeat/errors and implement reset/migrate executor actions.
4. **Runtime layer:** advance checkpoint generation after a model/job reaches a recoverable boundary.
5. **Distributed layer (Phase 9):** feed cluster-node telemetry into `nx_node_table_t`; use the selected target when executing `NX_RECOVERY_MIGRATE`.
6. **Security layer:** map kernel principals/capabilities to `nx_security_context_t`; do not grant panic/quarantine/migration capabilities to ordinary workloads.
7. **Telemetry:** export audit events to the diagnostics pipeline without making the audit ring depend on telemetry availability.

## Production invariants

- No privileged recovery action is executed before action-derived authorization.
- Executor-dependent recovery cannot report success without a bound executor.
- A rollback checkpoint is never used unless committed, owner-matched and integrity-valid.
- A prepared checkpoint record is not evicted before commit/invalidate.
- Quarantined components are not automatically returned to service.
- Unreachable/quarantined nodes are excluded from migration selection.
- Recovery escalation is bounded by policy counters.
- Equal recovery-node scores have a stable node-ID tie-break.
- The health/watchdog path requires no dynamic allocation.

## Concurrency note

The Phase 10 reference code deliberately leaves locking ownership to the kernel integration layer. In a real SMP build, registry, checkpoint store and audit-ring mutations must be protected using the synchronization primitives already established in Nexora. Standalone test success is not evidence of SMP safety.
