# Phase 17 acceptance criteria

Phase 17 is accepted only if all of the following hold:

1. The C11 health registry compiles with warnings promoted to errors.
2. Health aggregation escalates FAILED > DEGRADED > OK > UNKNOWN correctly.
3. The trace ring retains the latest 1,024 events and returns monotonic sequence numbers.
4. Watchdog misses are edge-counted: a continuously overdue target increments its miss count once until a heartbeat clears it.
5. The smoke benchmark is deterministic across repeated runs.
6. A SHA-256 manifest can be generated and independently verified.
7. The release gate completes with exit code 0.
8. No Phase 17 subsystem starts threads, allocates dynamic memory, or requires wall-clock services internally.
9. Time is injected by the caller in nanoseconds; the kernel remains responsible for the monotonic clock source.
10. Phase 17 remains additive: existing scheduler, allocator, IPC, device, distributed, and security interfaces need not be rewritten.
