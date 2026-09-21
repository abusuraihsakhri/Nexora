# Phase 13 Integration Guide

## 1. Add sources

Add:

```text
include/ai/observability.h
include/ai/obs_reliability.h
src/ai/observability.c
src/ai/obs_reliability.c
```

Keep the existing Phase 12 reliability files unchanged.

## 2. Initialize after the monotonic clock is available

```c
static ai_observability g_obs;

ai_obs_init(&g_obs, monotonic_now_ns());
```

Use static/global or explicitly reserved kernel storage. The full observability object is tens of KiB and should not be placed on a small kernel stack.

Metric registration should happen during subsystem initialization, not on every hot-path event.

## 3. Use stable numeric metric keys

Keep the human-readable mapping outside the core, for example:

```text
0x1001 scheduler.dispatch.total
0x1002 scheduler.queue.depth
0x1003 scheduler.pick.latency_ns
0x2001 device.bytes.transferred
0x2002 device.exec.latency_ns
```

Do not derive keys from pointers or unstable boot-time addresses.

## 4. Preserve authorization order

The control path remains:

```text
capability/isolation authorization
  -> Phase 12 reliability admission
  -> scheduler/device action
  -> Phase 13 observation
```

Diagnostic export requires its own Phase 11 authorization before calling the Phase 13 clearance filter.

## 5. Instrument scheduler/device hot paths conservatively

Recommended initial metrics:

- work submitted/completed/failed counters;
- queue depth gauge;
- scheduler decision latency histogram;
- transfer bytes counter;
- transfer latency histogram;
- device execution latency histogram;
- recovery latency histogram;
- safe-mode entry counter.

Avoid tracing every tensor element, page fault, or cache operation until overhead is measured.

## 6. Correlation IDs

Use a stable work/request/graph identifier as `correlation_id`. Do not use raw kernel pointers. This permits scheduler, transfer, device, and recovery events to be joined without leaking addresses.

## 7. Reliability mirroring

Maintain one cursor per observability sink:

```c
static ai_obs_rel_cursor rel_cursor;
ai_obs_rel_cursor_init(&rel_cursor);

ai_obs_mirror_reliability(
    &g_obs,
    &g_reliability,
    &rel_cursor,
    32u,
    monotonic_now_ns()
);
```

A bounded `max_events` prevents reliability mirroring from monopolizing a kernel control path.

## 8. Export

For a user/debug ABI:

1. authorize the caller with Phase 11 capability policy;
2. choose a maximum visibility level;
3. copy data through `ai_obs_metric_read()` or `ai_obs_trace_export()`;
4. validate destination user buffers in the ABI layer;
5. never expose the internal `ai_observability *` or raw kernel pointers.

## 9. Locking

Phase 13 has no internal locking. Call it under an owning lock or from a single-writer telemetry path. Do not assume concurrent writers are safe.

## 10. Crash/recovery use

Capture `ai_obs_snapshot` alongside Phase 12 checkpoint/reliability metadata. The snapshot is intentionally metadata-only; payload persistence remains external. Treat the returned snapshot as trusted diagnostic data: its checksums fingerprint all stored telemetry, including sensitive telemetry, and the corresponding trace record is labeled `SENSITIVE`.
