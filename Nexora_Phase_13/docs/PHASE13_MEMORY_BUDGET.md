# Phase 13 Memory Budget

Phase 13 intentionally trades bounded static memory for deterministic behavior.

On the x86-64 validation host used for this package, the C layout was:

```text
sizeof(ai_obs_metric)       =   360 bytes
sizeof(ai_obs_trace_event)  =    80 bytes
sizeof(ai_obs_span)         =    56 bytes
sizeof(ai_observability)    = 67,664 bytes
sizeof(ai_reliability)      = 19,552 bytes
```

The exact size is ABI/alignment dependent, but capacities are compile-time constants:

```text
64 metrics
512 trace events
64 active spans
16 finite histogram bounds (+ overflow bucket)
```

## Integration rule

`ai_observability` is intended for static/global or deliberately allocated kernel storage. Do **not** place the full object on a small kernel stack.

If later measurements show that ~66 KiB per observability instance is excessive, reduce capacities or move to a per-CPU/shared ring design in a future phase. Do not introduce hidden heap allocation merely to make the structure appear smaller at the call site.
