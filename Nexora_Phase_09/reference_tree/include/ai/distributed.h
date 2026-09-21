#ifndef NEXORA_AI_DISTRIBUTED_H
#define NEXORA_AI_DISTRIBUTED_H

#include <kernel/types.h>
#include <ai/device.h>
#include <ai/tensor.h>

/*
 * Nexora Phase 9: Distributed AI Resources
 *
 * This module is a control-plane/resource-model abstraction. It deliberately
 * does not implement a TCP/IP or RDMA driver. Concrete transports can bind to
 * the plans produced here in later phases without changing the graph-facing
 * object model.
 */

#define AI_DIST_MAX_ENDPOINTS       32
#define AI_DIST_MAX_LINKS          128
#define AI_DIST_MAX_REMOTE_TENSORS 256
#define AI_DIST_MAX_COLLECTIVES     64
#define AI_DIST_MAX_PARTICIPANTS    16
#define AI_DIST_MAX_PATH_HOPS        16
#define AI_DIST_MAX_REPLICAS          8

#define AI_DIST_INVALID_ID 0ull
#define AI_DIST_INF_NS     (~0ull / 4ull)

typedef enum {
    AI_DIST_HEALTH_HEALTHY = 0,
    AI_DIST_HEALTH_DEGRADED,
    AI_DIST_HEALTH_DOWN
} ai_dist_health;

typedef enum {
    AI_DIST_TRANSPORT_SIMULATED = 0,
    AI_DIST_TRANSPORT_SHM,
    AI_DIST_TRANSPORT_TCP,
    AI_DIST_TRANSPORT_RDMA,
    AI_DIST_TRANSPORT_CXL_FABRIC
} ai_dist_transport;

typedef enum {
    AI_DIST_RESOURCE_ENDPOINT = 0,
    AI_DIST_RESOURCE_REMOTE_TENSOR,
    AI_DIST_RESOURCE_NETWORK_LINK,
    AI_DIST_RESOURCE_COLLECTIVE
} ai_dist_resource_kind;

typedef struct {
    ai_dist_resource_kind kind;
    u64 id;
} ai_dist_resource_ref;

typedef struct {
    u64 id;
    const char *name;
    ai_device_mask device_kind;
    bool local;
    u32 numa_domain;

    u64 memory_total_bytes;
    u64 memory_free_bytes;
    u64 queue_delay_ns;

    /* Abstract throughput score: work units completed per second. */
    u64 compute_units_per_s;

    ai_dist_health health;
} ai_dist_endpoint;

typedef struct {
    u64 id;
    u64 src_endpoint_id;
    u64 dst_endpoint_id;
    ai_dist_transport transport;

    u64 bandwidth_bytes_per_s;
    u64 base_latency_ns;
    u64 policy_penalty_ns;

    bool bidirectional;
    ai_dist_health health;
} ai_dist_link;

typedef enum {
    AI_REMOTE_TENSOR_READONLY   = 1u << 0,
    AI_REMOTE_TENSOR_PINNED     = 1u << 1,
    AI_REMOTE_TENSOR_REPLICATED = 1u << 2,
    AI_REMOTE_TENSOR_COHERENT   = 1u << 3
} ai_remote_tensor_flags;

typedef struct {
    u64 id;
    const char *name;

    /* ID of the tensor in its originating Nexora domain, if one exists. */
    u64 origin_tensor_id;

    u64 owner_endpoint_id;
    u64 bytes;
    u64 generation;
    u32 flags;

    u64 replica_endpoint_ids[AI_DIST_MAX_REPLICAS];
    u32 replica_count;

    ai_dist_health health;
} ai_remote_tensor;

typedef struct {
    bool valid;
    u64 src_endpoint_id;
    u64 dst_endpoint_id;

    u64 endpoint_ids[AI_DIST_MAX_PATH_HOPS + 1];
    u64 link_ids[AI_DIST_MAX_PATH_HOPS];
    u32 hop_count;

    u64 estimated_ns;
    u64 bottleneck_bandwidth_bytes_per_s;
} ai_dist_path;

typedef enum {
    /* Multi-hop transfer is opt-in; otherwise path planning is direct-only. */
    AI_DIST_TRANSFER_ALLOW_STAGING = 1u << 0,
    /* Explicit hard direct-only policy for callers/executors that need it recorded. */
    AI_DIST_TRANSFER_REQUIRE_DIRECT = 1u << 1,
    AI_DIST_TRANSFER_PREFER_RDMA    = 1u << 2,
    AI_DIST_TRANSFER_COHERENT       = 1u << 3
} ai_dist_transfer_flags;

typedef struct {
    u64 src_endpoint_id;
    u64 dst_endpoint_id;
    u64 remote_tensor_id;
    u64 bytes;
    u32 flags;
    u64 deadline_budget_ns;
} ai_dist_transfer_request;

typedef struct {
    bool feasible;
    ai_dist_transfer_request request;
    ai_dist_path path;
    u64 estimated_ns;
    bool deadline_miss;
} ai_dist_transfer_plan;

typedef struct {
    ai_device_mask required_device_kind;
    u64 required_memory_bytes;

    /* Compute estimate expressed in the same abstract unit as endpoint rate. */
    u64 compute_units;

    /* Optional data source. 0 means no transfer cost is required. */
    u64 data_endpoint_id;
    u64 input_bytes;

    /* 0 means no deadline constraint. */
    u64 deadline_budget_ns;

    bool prefer_local;
} ai_dist_placement_request;

typedef struct {
    bool feasible;
    u64 endpoint_id;

    u64 transfer_ns;
    u64 queue_ns;
    u64 execution_ns;
    u64 memory_pressure_penalty_ns;
    u64 locality_penalty_ns;
    u64 deadline_penalty_ns;
    u64 total_score_ns;
    bool deadline_miss;
} ai_dist_placement_result;

typedef enum {
    AI_COLLECTIVE_BROADCAST = 0,
    AI_COLLECTIVE_REDUCE,
    AI_COLLECTIVE_ALLREDUCE,
    AI_COLLECTIVE_ALLGATHER,
    AI_COLLECTIVE_REDUCE_SCATTER,
    AI_COLLECTIVE_BARRIER
} ai_collective_kind;

typedef enum {
    AI_COLLECTIVE_ALGO_TREE = 0,
    AI_COLLECTIVE_ALGO_RING
} ai_collective_algorithm;

typedef struct {
    u64 id;
    const char *name;
    ai_collective_kind kind;

    u64 participant_endpoint_ids[AI_DIST_MAX_PARTICIPANTS];
    u32 participant_count;

    u64 root_endpoint_id;
    u64 bytes;
    ai_collective_algorithm algorithm;
    ai_dist_health health;
} ai_dist_collective;

typedef struct {
    bool feasible;
    u64 collective_id;
    ai_collective_algorithm algorithm;
    u64 estimated_ns;
    u32 communication_rounds;
    u64 critical_path_ns;
} ai_dist_collective_plan;

typedef struct {
    u64 placements_attempted;
    u64 placements_succeeded;
    u64 transfers_planned;
    u64 collectives_planned;
    u64 path_failures;
    u64 deadline_misses_predicted;
} ai_dist_metrics;

typedef struct {
    ai_dist_endpoint endpoints[AI_DIST_MAX_ENDPOINTS];
    u32 endpoint_count;

    ai_dist_link links[AI_DIST_MAX_LINKS];
    u32 link_count;

    ai_remote_tensor remote_tensors[AI_DIST_MAX_REMOTE_TENSORS];
    u32 remote_tensor_count;

    ai_dist_collective collectives[AI_DIST_MAX_COLLECTIVES];
    u32 collective_count;

    u64 next_endpoint_id;
    u64 next_link_id;
    u64 next_remote_tensor_id;
    u64 next_collective_id;

    ai_dist_metrics metrics;
} ai_dist_registry;

void ai_dist_init(ai_dist_registry *registry);

ai_dist_endpoint *ai_dist_add_endpoint(
    ai_dist_registry *registry,
    const char *name,
    ai_device_mask device_kind,
    bool local,
    u32 numa_domain,
    u64 memory_total_bytes,
    u64 memory_free_bytes,
    u64 compute_units_per_s,
    u64 queue_delay_ns
);

ai_dist_link *ai_dist_add_link(
    ai_dist_registry *registry,
    u64 src_endpoint_id,
    u64 dst_endpoint_id,
    ai_dist_transport transport,
    u64 bandwidth_bytes_per_s,
    u64 base_latency_ns,
    u64 policy_penalty_ns,
    bool bidirectional
);

ai_remote_tensor *ai_dist_publish_tensor(
    ai_dist_registry *registry,
    const char *name,
    const ai_tensor *tensor,
    u64 owner_endpoint_id,
    u64 generation,
    u32 flags
);

ai_remote_tensor *ai_dist_add_remote_tensor(
    ai_dist_registry *registry,
    const char *name,
    u64 origin_tensor_id,
    u64 owner_endpoint_id,
    u64 bytes,
    u64 generation,
    u32 flags
);

bool ai_dist_tensor_add_replica(
    ai_dist_registry *registry,
    u64 remote_tensor_id,
    u64 endpoint_id
);

ai_dist_collective *ai_dist_add_collective(
    ai_dist_registry *registry,
    const char *name,
    ai_collective_kind kind,
    const u64 *participant_endpoint_ids,
    u32 participant_count,
    u64 root_endpoint_id,
    u64 bytes
);

const ai_dist_endpoint *ai_dist_get_endpoint(const ai_dist_registry *registry, u64 endpoint_id);
ai_dist_endpoint *ai_dist_get_endpoint_mut(ai_dist_registry *registry, u64 endpoint_id);
const ai_dist_link *ai_dist_get_link(const ai_dist_registry *registry, u64 link_id);
const ai_remote_tensor *ai_dist_get_remote_tensor(const ai_dist_registry *registry, u64 remote_tensor_id);
ai_remote_tensor *ai_dist_get_remote_tensor_mut(ai_dist_registry *registry, u64 remote_tensor_id);
const ai_dist_collective *ai_dist_get_collective(const ai_dist_registry *registry, u64 collective_id);

bool ai_dist_set_endpoint_health(ai_dist_registry *registry, u64 endpoint_id, ai_dist_health health);
bool ai_dist_set_link_health(ai_dist_registry *registry, u64 link_id, ai_dist_health health);
bool ai_dist_update_endpoint_load(
    ai_dist_registry *registry,
    u64 endpoint_id,
    u64 memory_free_bytes,
    u64 queue_delay_ns
);

u64 ai_dist_estimate_link_ns(const ai_dist_link *link, u64 bytes);

bool ai_dist_find_path(
    ai_dist_registry *registry,
    u64 src_endpoint_id,
    u64 dst_endpoint_id,
    u64 bytes,
    u32 transfer_flags,
    ai_dist_path *out_path
);

bool ai_dist_plan_transfer(
    ai_dist_registry *registry,
    const ai_dist_transfer_request *request,
    ai_dist_transfer_plan *out_plan
);

bool ai_dist_place(
    ai_dist_registry *registry,
    const ai_dist_placement_request *request,
    ai_dist_placement_result *out_result
);

bool ai_dist_plan_collective(
    ai_dist_registry *registry,
    u64 collective_id,
    ai_dist_collective_plan *out_plan
);

bool ai_dist_validate(const ai_dist_registry *registry);

const ai_dist_metrics *ai_dist_get_metrics(const ai_dist_registry *registry);

const char *ai_dist_health_name(ai_dist_health health);
const char *ai_dist_transport_name(ai_dist_transport transport);
const char *ai_collective_kind_name(ai_collective_kind kind);
const char *ai_collective_algorithm_name(ai_collective_algorithm algorithm);

#endif
