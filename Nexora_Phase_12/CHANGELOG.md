# Changelog

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
