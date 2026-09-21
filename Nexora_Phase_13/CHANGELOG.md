# Changelog

## Phase 13

- Added fixed-capacity observability subsystem.
- Added counters, gauges, and bounded histograms with saturating arithmetic.
- Added stable metric keys, units, scopes, and visibility classes.
- Added structured trace ring with overwrite accounting.
- Added correlation IDs and bounded latency spans.
- Added clearance-aware metric reads and trace export.
- Added deterministic trace and metric checksums.
- Added metadata-only diagnostic snapshots.
- Added Phase 12 reliability-journal mirror with explicit source-gap detection.
- Added Phase 13 unit/edge tests, sanitizer validation, freestanding compile gate, and combined-core unresolved-symbol gate.
- Preserved Phase 12 regression tests and Phase 11/12 control-plane ownership contracts.

## Phase 12

- Added reliability-domain registry for agents, devices, remote nodes, network paths, work graphs and services.
- Added explicit HEALTHY/DEGRADED/QUARANTINED/RECOVERING/FAILED state machine.
- Added bounded fault escalation and quarantine policy.
- Added heartbeat timeout detection.
- Added safe-mode admission control state.
- Added deterministic recovery action selection.
- Added bounded replay journal with overwrite accounting.
- Added replay checksum and checkpoint metadata.
- Added host tests, policy-edge tests and freestanding compile gate.
- Added cross-phase contracts to prevent reliability recovery from weakening Phase 11 security.
