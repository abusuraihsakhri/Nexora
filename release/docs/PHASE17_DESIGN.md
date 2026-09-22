# Phase 17 design notes

## Design constraints

The instrumentation path must remain bounded. Therefore all kernel-facing storage is statically allocated, event records are fixed-size, and no formatter or serializer runs on the hot path.

## Health model

Health state is observational rather than prescriptive. A component publishes state plus a diagnostic code and timestamp. Aggregation follows failure dominance: FAILED, then DEGRADED, then all-OK, otherwise UNKNOWN.

## Trace model

Writers reserve a monotonically increasing sequence and overwrite the corresponding ring slot. Slot payload fields are atomic and each slot has a tiny writer lock so two producers cannot interleave writes after ring wrap. Publication occurs only after the payload is written. Readers verify the published sequence before and after copying; overwritten/torn entries are skipped. The structure is intended for diagnostics, not durable audit logging.

## Watchdog model

The watchdog has no private timer thread. The kernel calls `nx_watchdog_check(now_ns, ...)` from its existing supervision path. Miss counters increase on the transition into overdue state, not on every check while overdue.

## Deliberate non-goals

Phase 17 does not implement panic/reboot policy, persistent telemetry, remote metrics export, user-space logging daemons, or device reset logic. Those mechanisms must consume Phase 17 signals through explicit policy layers.
