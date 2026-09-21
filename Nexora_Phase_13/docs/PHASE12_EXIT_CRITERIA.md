# Phase 12 Exit Criteria

Phase 12 is complete when all of the following are true in the integrated Nexora tree:

- [ ] Reliability domain registry builds without dynamic allocation.
- [ ] Agent, device, remote-node and work-graph domains can be registered.
- [ ] Warning/error/fatal faults produce deterministic health transitions.
- [ ] Repeated failures are bounded by explicit retry and quarantine thresholds.
- [ ] Heartbeat loss is detected from the monotonic kernel clock.
- [ ] Failed/quarantined resources are excluded from ordinary scheduling admission.
- [ ] Safe mode blocks non-critical new work without bypassing capability checks.
- [ ] Recovery choices are journaled before execution.
- [ ] Device reset / remote quarantine / graph rollback remain delegated to their owning subsystems.
- [ ] Replay journal uses fixed memory and reports overwrite count.
- [ ] Checkpoint metadata includes replay position and journal checksum.
- [ ] Host unit tests pass with `-Wall -Wextra -Wpedantic -Werror`.
- [ ] Core implementation compiles with `-ffreestanding -fno-builtin`.
- [ ] No libc symbols or heap allocation are introduced by the Phase 12 core.
- [ ] Cross-phase review confirms no Phase 11 authorization path is weakened by recovery.

## Non-goals

The following are intentionally deferred:

- full tensor/model payload checkpoint storage
- distributed consensus / Raft / Paxos
- transparent exactly-once remote execution
- production crash dump filesystem
- kernel-wide tracing framework
- automatic hot migration of GPU execution state
- self-healing that bypasses explicit policy

These are separate research problems and should not be hidden inside a reliability milestone.
