# Nexora Phase 12 — Reliability, Fault Containment, Recovery and Replay

## Objective

Phase 12 adds a kernel-level reliability control plane around the AI-native execution model. Earlier phases can create tensors, schedule work, address heterogeneous and distributed resources, isolate agents, and enforce capability policy. Phase 12 answers the next operational question:

> What happens when a device, agent, remote node, network path, work graph, or service becomes unhealthy while AI work is executing?

The design is intentionally small and deterministic. It does not attempt to build a full observability stack or distributed consensus system inside the kernel.

## Core invariants

1. **No silent failure.** Every reliability-relevant state change is journaled.
2. **Bounded retries.** Recovery never retries indefinitely.
3. **Quarantine before cascade.** Repeated or suspicious failures remove a resource from admission.
4. **Fatal invariant violations fail closed.** Fatal kernel or capability faults move the domain to `FAILED`.
5. **No hidden allocation.** The subsystem uses fixed-capacity registries and a bounded ring journal.
6. **Deterministic policy.** Given the same state, event order, policy, and timestamps, recovery decisions are reproducible.
7. **Payload checkpoints stay outside this subsystem.** Phase 12 records checkpoint metadata only; tensor/model payload persistence remains owned by the memory/runtime layers.

## Reliability domain model

Each failure-containment unit is represented by an `ai_rel_domain`:

- agent
- device
- remote node
- network path
- work graph
- service

A domain moves through:

```text
HEALTHY
   |
   | warning/error
   v
DEGRADED
   |
   | repeated errors / policy threshold
   v
QUARANTINED -----------+
   |                    |
   | quarantine expiry  | recovery attempt
   v                    v
DEGRADED           RECOVERING
                        |
                 +------+------+
                 |             |
              success        failure
                 |             |
                 v             v
              HEALTHY    QUARANTINED/FAILED
```

A fatal fault can transition directly to `FAILED`.

## Policy

The default policy contains only measurable thresholds:

- failures before degradation
- failures before quarantine
- failures before permanent domain failure
- maximum retry attempts
- quarantine duration
- heartbeat timeout
- number of quarantined/failed domains that trigger safe mode

Policy is passed to `ai_rel_init()` and sanitized so zero/invalid threshold combinations cannot create retry loops or disabled liveness checking by accident.

## Deterministic replay journal

The Phase 12 journal records execution-control metadata, not tensor payloads. Typical records include:

- scheduler selection
- resource assignment
- work start/finish/failure
- fault observation
- health-state transition
- recovery decision
- checkpoint boundary
- safe-mode entry/exit

The journal is a bounded ring. When full, the oldest event is overwritten and `dropped_events` increments. This makes memory consumption deterministic while preserving explicit evidence that history was lost.

A stable FNV-1a-derived checksum over the active journal can be attached to test artifacts and checkpoint metadata to detect divergent execution histories.

## Safe mode

Safe mode is a conservative admission state. It activates when the configured number of domains are quarantined or failed.

While safe mode is active:

- non-critical new work should be rejected by the scheduler/admission layer;
- critical recovery/control work may continue;
- failed and recovering domains remain non-admissible;
- existing capability/security policy still applies.

Safe mode is not a replacement for Phase 11 security controls. It is a reliability decision layered after authorization.

## Recovery policy

The reference decision policy is:

- fatal fault -> fail domain
- capability violation / kernel invariant fault -> quarantine (or fail if fatal)
- resource exhaustion -> abort work instead of retrying blindly
- transient faults -> bounded retry
- device fault after retry budget -> reset resource
- network/remote fault after retry budget -> quarantine
- work failure / scheduler stall after retry budget -> rollback to external checkpoint

Actual device reset, graph rollback, tensor reconstruction, or remote reconnect operations are performed by the owning subsystem. Phase 12 chooses and journals the action; it does not impersonate those subsystems.

## Concurrency contract

The current implementation is deliberately lock-free in the sense that it contains no internal locking primitives. That does **not** make it multi-writer safe.

Integration rule:

> Call Phase 12 APIs while holding the owning scheduler/resource-management lock, or provide a single-writer reliability event path.

This avoids prematurely coupling the reliability layer to a particular SMP locking implementation.

## Security interaction

Reliability must not weaken Phase 11 policy:

```text
request
  -> capability / isolation authorization
  -> reliability admission
  -> scheduler / device dispatch
```

A retry or recovery action is subject to the same or stricter capability checks as the failed operation. Recovery must never become a privilege-escalation path.

## Distributed interaction

For Phase 9 remote resources:

- each remote node gets a reliability domain;
- network paths may receive separate domains when topology-aware routing is enabled;
- heartbeat loss marks the relevant domain degraded or quarantined;
- remote retry budgets are bounded;
- the scheduler must not place new work on a quarantined remote domain.

This provides a local kernel failure-containment mechanism without claiming distributed consensus or exactly-once execution semantics.
