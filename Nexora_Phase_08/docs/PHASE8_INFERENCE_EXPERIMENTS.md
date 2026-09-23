# Phase 8 — Inference-Oriented Experiments

## Scope

Phase 8 implements the inference-oriented milestone defined in the original Nexora roadmap. It adds bounded kernel-side metadata and policy primitives for:

- persistent model residency;
- KV-cache residency and session ownership;
- request admission under memory pressure;
- deadline-aware dynamic batching;
- tensor prefetch planning;
- explicit accounting of committed inference memory.

The implementation intentionally remains a research control plane. It does not claim to execute a real LLM, replace vLLM, or provide a production GPU runtime.

## Architecture

```text
request
  │
  ├─ admission control ── memory budget/high-watermark
  │
  ├─ model residency ──── persistent model object
  │
  ├─ KV state ─────────── session/model residency metadata
  │
  └─ queued request
         │
         ▼
 deadline-aware batch builder
         │
         ▼
 Phase 7 accelerator/device path
```

## Invariants

1. A request cannot be admitted for an unknown or non-resident model.
2. Model weights, KV state, and admitted request working memory share one bounded accounting budget.
3. Admission never crosses the configured high-watermark.
4. Batch formation is model-local and bounded by both request count and input-token budget.
5. Within eligible requests, earlier deadlines are selected first; enqueue time breaks ties.
6. Expired queued requests are rejected and their committed working-memory budget is released.
7. Completing an admitted request releases its estimated working-memory commitment.
8. Prefetch plans are advisory: Phase 8 determines whether capacity permits destination residency; actual transport remains delegated to the device path.

## Deliberate boundaries

Phase 8 does **not** yet implement a tokenizer/model graph parser, actual KV-cache page allocation or compaction, vLLM internals, CUDA/ROCm/PJRT execution, bandwidth-derived prefetch completion prediction, or production SMP synchronization.

## Validation

```sh
make phase8-host-test
make phase8-check
```

The host test covers model residency, KV tracking, memory-pressure admission, deadline ordering, batch limits, completion accounting, expired-request rejection, and prefetch-plan validation. `phase8-check` additionally compiles/links the freestanding kernel and verifies Phase 8 symbols in the ELF.

## Phase 9 contract

Phase 9's distributed planner consumes these concepts without depending on their internals: model residency becomes endpoint-local persistent state; KV state can be published/replicated; dynamic batches can call distributed placement; prefetch plans can be routed through distributed transfer planning; and distributed placement failure feeds admission control rather than silently overcommitting a remote endpoint.
