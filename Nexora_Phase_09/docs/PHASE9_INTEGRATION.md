# Phase 9 integration into an existing Phase 8 tree

The Phase 9 module is intentionally additive.

## Files to copy

```text
module/include/ai/distributed.h -> include/ai/distributed.h
module/src/ai/distributed.c     -> src/ai/distributed.c
```

Add `src/ai/distributed.c` to the kernel source list.

## Required pre-existing ABI

The module depends only on these existing public concepts:

```text
kernel/types.h
ai/device.h    -> ai_device_mask and AI_DEVICE_* bits
ai/tensor.h    -> ai_tensor.id, .name, .bytes
```

It does not depend on Phase 8 batching, model, KV-cache, admission-control, or prefetch internals. Those systems can therefore integrate incrementally.

## Recommended Phase 8 hooks

### Persistent model objects

Register each accelerator/model residency as endpoint-local state. A model shard or weight tensor can be published as an `ai_remote_tensor`, then replicated to peer endpoints.

### KV-cache residency

Publish KV-cache objects with `AI_REMOTE_TENSOR_PINNED`. If replicas are permitted, track their endpoints with `ai_dist_tensor_add_replica()`.

### Dynamic batching

Before committing a batch to an endpoint, call `ai_dist_place()` with aggregate memory, work units, input size, source endpoint, and deadline budget.

### Prefetch

Use `ai_dist_plan_transfer()` to choose the fabric path and estimate whether prefetch can complete before its use point. Set `AI_DIST_TRANSFER_ALLOW_STAGING` explicitly when multi-hop movement is permitted; leave it unset (or use `AI_DIST_TRANSFER_REQUIRE_DIRECT`) when only a direct link is acceptable.

### Admission control

Treat `ai_dist_place()` failure as a distributed admission-control rejection rather than silently overcommitting a remote accelerator.

## Do not do yet

Do not bind this API directly to a specific RDMA/NCCL vendor ABI inside the core planner. Keep transport execution behind a narrow adapter in the next hardware/network phase.
