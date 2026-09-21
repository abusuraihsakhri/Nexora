# Phase 13 Cross-Phase Contracts

## Phase 2 — Tensor object/lifetime model

Observability may identify a tensor by stable object ID and record aggregate bytes/lifetime metrics. It must not copy tensor payloads or retain references that change tensor reclamation semantics.

## Phase 3 — Scheduler

The scheduler remains the dispatch authority. Phase 13 may measure queue depth, decisions, placements, latency, and outcomes, but telemetry must not reorder work or become a hidden scheduling policy.

## Phase 4 — Shared tensor handles

Trace correlation should use stable object/handle identifiers, never raw mapped addresses. Observability does not broaden shared-handle rights.

## Phase 5 — User ABI

Any telemetry syscall/debug ABI must validate user buffers and authorize access before Phase 13 visibility filtering. Raw internal structs/pointers are not a stable user ABI.

## Phase 6 — Linux comparison harness

Add comparable measurements for trace overhead, histogram update cost, scheduler latency, device execution latency, transfer latency, dropped traces, and diagnostic snapshot cost.

## Phase 7 — Device path

Drivers may emit transfer/device spans and counters. MMIO/DMA control remains in the driver; Phase 13 must never reset or reprogram a device.

## Phase 8 — Inference experiments

Measure model-load latency, batch formation, prefill/decode latency, KV-cache pressure, cache hit/miss/reconstruction, and deadline tail latency. Do not capture prompts, model weights, or KV payloads by default.

## Phase 9 — Distributed layer

Remote node/path metrics need stable IDs and bounded cardinality. Phase 13 can correlate transfer/retry events but does not provide distributed ordering, consensus, or exactly-once semantics.

## Phase 10 — Agent domains

Per-agent telemetry must remain scoped and capability-controlled. A metric or trace label is not evidence of malicious intent and must not change agent privileges automatically.

## Phase 11 — Security hardening

Mandatory ordering:

```text
authorize -> reliability-admit -> schedule/execute -> observe
```

Telemetry export is itself a protected operation. Visibility labels supplement, not replace, capability authorization. Never trace secrets, capability tokens, raw pointers, or arbitrary user memory by default.

## Phase 12 — Reliability

Phase 12 remains authoritative for health, quarantine, safe mode, retry budgets, and recovery decisions. Phase 13 may mirror the reliability journal for diagnostics, but it must not edit Phase 12 state or infer that missing mirrored events never occurred. Source loss is explicit through `AI_OBS_TRACE_SOURCE_GAP`.
