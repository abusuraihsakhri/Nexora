# Phase 12 Cross-Phase Contracts

## Phase 2 — Tensor object/lifetime model

Phase 12 does not own tensor lifetime. A recovery or rollback request must invoke the tensor manager to validate whether required tensor generations still exist or must be reconstructed.

## Phase 3 — Scheduler

The scheduler remains the dispatch authority. Phase 12 provides:

- resource admission state;
- recovery recommendation;
- deterministic event journal.

It must not directly enqueue, reorder, or execute work behind the scheduler's back.

## Phase 4 — Shared tensor handles

A quarantined or failed domain invalidates future placement decisions, not existing capability semantics. Shared mappings must be revoked/unmapped through the existing handle/capability mechanisms.

## Phase 5 — User ABI

User mode should receive stable error classes such as transient failure, quarantined resource, failed domain, or retry exhausted. Do not expose internal pointers or mutable Phase 12 state through the syscall ABI.

## Phase 6 — Linux comparison harness

Add measurements for:

- failure-detection latency;
- retry count;
- quarantine latency;
- successful recovery time;
- journal overhead;
- safe-mode admission effect.

## Phase 7 — Device path

Device drivers are the source of concrete reset/DMA/protocol faults. Phase 12 does not manipulate PCI/MMIO/DMA directly.

## Phase 8 — Inference experiments

Persistent models and KV caches need recovery classes:

- reconstructable cache;
- reloadable persistent model;
- non-replayable external side effect.

Rollback must not assume every work node is idempotent.

## Phase 9 — Distributed layer

Remote resources need heartbeats and domain IDs. Network retry is bounded and topology placement must exclude quarantined paths/nodes.

## Phase 10 — Agent domains

Agent failure domains may be distinct from device/resource domains. A failing device must not automatically mark an agent malicious; an agent capability violation must not be treated as a transient hardware error.

## Phase 11 — Security hardening

Ordering is mandatory:

```text
authorize -> reliability-admit -> schedule -> execute
```

Recovery operations must be re-authorized where they touch protected resources. No retry, reset, rollback, or migration path may broaden rights.
