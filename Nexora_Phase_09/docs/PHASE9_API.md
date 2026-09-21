# Phase 9 API reference

## Registry

`ai_dist_init()` initializes all resource tables and monotonically increasing object IDs.

## Endpoints

`ai_dist_add_endpoint()` registers a local or remote compute endpoint. An endpoint carries a device-kind mask, memory availability, queue delay, abstract compute throughput, NUMA domain, and health state.

`ai_dist_update_endpoint_load()` updates volatile memory/queue observations without replacing endpoint identity.

`ai_dist_set_endpoint_health()` supports HEALTHY, DEGRADED, and DOWN states.

## Links and topology

`ai_dist_add_link()` creates a directed or bidirectional fabric edge with transport, bandwidth, latency, policy penalty, and health.

`ai_dist_find_path()` runs a bounded Dijkstra search over endpoint/link metadata. Multi-hop staging is opt-in with `AI_DIST_TRANSFER_ALLOW_STAGING`; otherwise routing is direct-only. `AI_DIST_TRANSFER_REQUIRE_DIRECT` records an explicit hard direct-transfer policy. RDMA-like transports can be preferred without being required.

`ai_dist_plan_transfer()` wraps path selection in a transfer object and predicts deadline misses. If `remote_tensor_id` is nonzero, the tensor must exist, be available, fit the requested byte count, and reside at the transfer source as owner or replica.

## Remote tensors

`ai_dist_publish_tensor()` publishes an existing `ai_tensor` into the distributed resource domain without changing the tensor's local ABI.

`ai_dist_add_remote_tensor()` is the transport-neutral variant for a tensor that is known only by origin ID/size.

`ai_dist_tensor_add_replica()` tracks replicated residency locations.

## Placement

`ai_dist_place()` evaluates all feasible endpoints matching the required device mask and memory requirement. The score includes:

- input transfer cost
- queue delay
- execution estimate
- memory pressure
- local/remote preference
- deadline miss penalty

## Collectives

`ai_dist_add_collective()` represents broadcast, reduce, all-reduce, all-gather, reduce-scatter, or barrier as a first-class resource object. Rooted operations require the root to be a participant; optional roots on symmetric operations must also belong to the participant set.

`ai_dist_plan_collective()` estimates communication rounds and critical-path cost. Current policy uses trees for small/rooted operations and rings for larger symmetric collectives. Directed tree planning respects operation direction: broadcast uses root-to-peer, reduce uses peer-to-root, and synchronization/symmetric tree operations require both directions.

This is an initial research planner, not a replacement for NCCL/MPI semantics.

## Validation and metrics

`ai_dist_validate()` checks registry invariants and references.

`ai_dist_get_metrics()` exposes planning counters without telemetry or external I/O.
