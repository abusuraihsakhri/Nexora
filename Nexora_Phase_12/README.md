# Nexora Phase 12

**Reliability, Fault Containment, Recovery and Deterministic Replay**

This Phase 12 package adds a small freestanding reliability subsystem for the Nexora AI-native kernel architecture.

## Included

- fixed-capacity failure-domain registry
- explicit health state machine
- bounded retry/quarantine policy
- heartbeat/liveness detection
- safe-mode admission state
- deterministic recovery decision logic
- fixed-size replay journal
- journal checksum
- checkpoint metadata binding execution history to graph state
- host tests and freestanding compile gate
- cross-phase integration contracts

## Build and test

```bash
make test
```

Expected output:

```text
Phase 12 reliability tests: PASS
Phase 12 policy edge tests: PASS
```

The test target also compiles `src/ai/reliability.c` using:

```text
-ffreestanding -fno-builtin -Wall -Wextra -Wpedantic -Werror
```

## Merge into Nexora

Copy:

```text
include/ai/reliability.h
src/ai/reliability.c
```

into the current Phase 11 tree and add the source to the kernel build. Keep the Phase 11 `kernel/types.h`; the copy in this package only makes the package self-testable.

Read `docs/INTEGRATION.md` before wiring it into scheduler, device, distributed, and agent code.

## Deliberate boundary

Phase 12 records and governs recovery. It does **not** implement device reset internals, tensor payload checkpointing, distributed consensus, or privilege changes. Those remain owned by their existing subsystems.
