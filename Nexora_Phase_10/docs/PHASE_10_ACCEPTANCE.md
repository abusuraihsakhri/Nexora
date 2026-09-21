# Phase 10 Acceptance Criteria

Standalone Phase 10 is complete when all of the following are true:

- [x] Fixed-capacity health registry exists.
- [x] Heartbeat timeout transitions are deterministic.
- [x] Heartbeat-driven state transitions update the generation counter.
- [x] Consecutive failure escalation is implemented.
- [x] Watchdog converts failed health states into recovery decisions.
- [x] Recovery actions are capability-gated using action-derived authorization.
- [x] Forged `required_capability` metadata cannot bypass authorization.
- [x] Executor-dependent recovery fails closed when no executor is bound.
- [x] Recovery decisions/results/security denials are auditable.
- [x] Checkpoints implement prepare -> commit -> select-latest lifecycle.
- [x] Corrupted checkpoint metadata is rejected.
- [x] Rollback/carry-over checkpoint IDs are subject/owner/integrity validated.
- [x] Prepared checkpoints are not silently evicted under capacity pressure.
- [x] Distributed migration excludes unreachable/quarantined nodes.
- [x] Recovery target selection is deterministic, including equal-score tie-breaking.
- [x] `rollback_after` participates in escalation policy.
- [x] Reference tests compile with strict warnings-as-errors.
- [x] Adversarial/security/boundary tests pass.
- [x] GCC ASan/UBSan test run passes.
- [x] Clang strict build passes.
- [x] Phase 10 sources compile with `-ffreestanding -fno-builtin`.
- [x] Freestanding objects have no libc `memset` dependency.
- [x] Independent CMake/CTest build passes.

Integration-only gates still required against the complete Phase 1–9 tree:

- [ ] Bind Nexora monotonic clock.
- [ ] Bind Phase 9 migration executor.
- [ ] Bind scheduler/task restart path.
- [ ] Bind checkpoint payload backend.
- [ ] Bind accelerator/device recovery paths.
- [ ] Add existing Nexora SMP synchronization primitives.
- [ ] Wire authoritative health/checkpoint/node-selection audit events if required by the global telemetry contract.
- [ ] Run whole-tree ABI/API compile.
- [ ] Run whole-tree boot/emulator/hardware regression suite.
