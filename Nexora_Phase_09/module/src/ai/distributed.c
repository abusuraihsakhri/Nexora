#include <ai/distributed.h>

static u64 sat_add(u64 a, u64 b) {
    if (a >= AI_DIST_INF_NS || b >= AI_DIST_INF_NS) return AI_DIST_INF_NS;
    if (a > AI_DIST_INF_NS - b) return AI_DIST_INF_NS;
    return a + b;
}

static u64 sat_mul(u64 a, u64 b) {
    if (a == 0 || b == 0) return 0;
    if (a >= AI_DIST_INF_NS || b >= AI_DIST_INF_NS) return AI_DIST_INF_NS;
    if (a > AI_DIST_INF_NS / b) return AI_DIST_INF_NS;
    return a * b;
}

static bool valid_health(ai_dist_health health) {
    return health == AI_DIST_HEALTH_HEALTHY ||
           health == AI_DIST_HEALTH_DEGRADED ||
           health == AI_DIST_HEALTH_DOWN;
}

static bool valid_transport(ai_dist_transport transport) {
    return transport >= AI_DIST_TRANSPORT_SIMULATED &&
           transport <= AI_DIST_TRANSPORT_CXL_FABRIC;
}

static bool valid_collective_kind(ai_collective_kind kind) {
    return kind >= AI_COLLECTIVE_BROADCAST &&
           kind <= AI_COLLECTIVE_BARRIER;
}

static bool valid_collective_algorithm(ai_collective_algorithm algorithm) {
    return algorithm == AI_COLLECTIVE_ALGO_TREE ||
           algorithm == AI_COLLECTIVE_ALGO_RING;
}

/*
 * Compute ceil(a*b/c) without 128-bit division or libgcc helpers.
 *
 * The quotient part (a/c)*b is handled directly.  For the fractional
 * remainder (a%c)*b/c, a bitwise long-multiplication recurrence keeps the
 * intermediate remainder strictly below c, so no overflowing r*b product is
 * formed.  This matters in a -nostdlib kernel link where __udivti3 is not
 * available.
 */
static u64 mul_div_ceil_u64(u64 a, u64 b, u64 c) {
    if (c == 0) return AI_DIST_INF_NS;

    u64 whole = a / c;
    u64 multiplicand = a % c;
    u64 result = sat_mul(whole, b);
    if (result >= AI_DIST_INF_NS) return AI_DIST_INF_NS;

    u64 q = 0;
    u64 rem = 0;

    for (i32 bit = 63; bit >= 0; --bit) {
        u32 carry = 0;

        /* rem = (2*rem) mod c, carry += floor(2*rem/c). */
        if (rem >= c - rem) {
            rem = rem - (c - rem);
            carry++;
        } else {
            rem += rem;
        }

        if ((b >> (u32)bit) & 1ull) {
            /* rem = (rem + multiplicand) mod c without overflow. */
            if (rem >= c - multiplicand) {
                rem = rem - (c - multiplicand);
                carry++;
            } else {
                rem += multiplicand;
            }
        }

        if (q > (AI_DIST_INF_NS - (u64)carry) / 2ull) return AI_DIST_INF_NS;
        q = q * 2ull + (u64)carry;
    }

    result = sat_add(result, q);
    if (result >= AI_DIST_INF_NS) return AI_DIST_INF_NS;
    if (rem != 0) result = sat_add(result, 1);
    return result;
}

static i32 endpoint_index(const ai_dist_registry *registry, u64 endpoint_id) {
    if (!registry || endpoint_id == AI_DIST_INVALID_ID) return -1;
    for (u32 i = 0; i < registry->endpoint_count; ++i) {
        if (registry->endpoints[i].id == endpoint_id) return (i32)i;
    }
    return -1;
}

static i32 link_index(const ai_dist_registry *registry, u64 link_id) {
    if (!registry || link_id == AI_DIST_INVALID_ID) return -1;
    for (u32 i = 0; i < registry->link_count; ++i) {
        if (registry->links[i].id == link_id) return (i32)i;
    }
    return -1;
}

static i32 remote_tensor_index(const ai_dist_registry *registry, u64 remote_tensor_id) {
    if (!registry || remote_tensor_id == AI_DIST_INVALID_ID) return -1;
    for (u32 i = 0; i < registry->remote_tensor_count; ++i) {
        if (registry->remote_tensors[i].id == remote_tensor_id) return (i32)i;
    }
    return -1;
}

static i32 collective_index(const ai_dist_registry *registry, u64 collective_id) {
    if (!registry || collective_id == AI_DIST_INVALID_ID) return -1;
    for (u32 i = 0; i < registry->collective_count; ++i) {
        if (registry->collectives[i].id == collective_id) return (i32)i;
    }
    return -1;
}

static bool link_allows_direction(const ai_dist_link *link, u64 from, u64 to) {
    if (!link) return false;
    if (link->src_endpoint_id == from && link->dst_endpoint_id == to) return true;
    if (link->bidirectional &&
        link->src_endpoint_id == to && link->dst_endpoint_id == from) return true;
    return false;
}

static bool link_transport_allowed(const ai_dist_link *link, u32 flags) {
    if (!link) return false;
    if ((flags & AI_DIST_TRANSFER_PREFER_RDMA) == 0) return true;

    /* RDMA is a preference, not a hard requirement. Penalization is handled by path cost. */
    return true;
}

static u64 link_policy_cost(const ai_dist_link *link, u32 flags, u64 bytes) {
    u64 cost = ai_dist_estimate_link_ns(link, bytes);
    if (cost >= AI_DIST_INF_NS) return AI_DIST_INF_NS;

    if ((flags & AI_DIST_TRANSFER_PREFER_RDMA) &&
        link->transport != AI_DIST_TRANSPORT_RDMA &&
        link->transport != AI_DIST_TRANSPORT_CXL_FABRIC &&
        link->transport != AI_DIST_TRANSPORT_SHM) {
        cost = sat_add(cost, 500000ull); /* 0.5 ms soft preference penalty. */
    }

    if (link->health == AI_DIST_HEALTH_DEGRADED) {
        cost = sat_add(cost, 1000000ull); /* 1 ms risk/quality penalty. */
    }

    return cost;
}

void ai_dist_init(ai_dist_registry *registry) {
    if (!registry) return;

    registry->endpoint_count = 0;
    registry->link_count = 0;
    registry->remote_tensor_count = 0;
    registry->collective_count = 0;

    registry->next_endpoint_id = 1;
    registry->next_link_id = 1;
    registry->next_remote_tensor_id = 1;
    registry->next_collective_id = 1;

    registry->metrics.placements_attempted = 0;
    registry->metrics.placements_succeeded = 0;
    registry->metrics.transfers_planned = 0;
    registry->metrics.collectives_planned = 0;
    registry->metrics.path_failures = 0;
    registry->metrics.deadline_misses_predicted = 0;
}

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
) {
    if (!registry || registry->endpoint_count >= AI_DIST_MAX_ENDPOINTS) return NULL;
    if (device_kind == 0 || memory_free_bytes > memory_total_bytes) return NULL;
    if (compute_units_per_s == 0) return NULL;

    ai_dist_endpoint *endpoint = &registry->endpoints[registry->endpoint_count++];
    endpoint->id = registry->next_endpoint_id++;
    endpoint->name = name;
    endpoint->device_kind = device_kind;
    endpoint->local = local;
    endpoint->numa_domain = numa_domain;
    endpoint->memory_total_bytes = memory_total_bytes;
    endpoint->memory_free_bytes = memory_free_bytes;
    endpoint->queue_delay_ns = queue_delay_ns;
    endpoint->compute_units_per_s = compute_units_per_s;
    endpoint->health = AI_DIST_HEALTH_HEALTHY;
    return endpoint;
}

ai_dist_link *ai_dist_add_link(
    ai_dist_registry *registry,
    u64 src_endpoint_id,
    u64 dst_endpoint_id,
    ai_dist_transport transport,
    u64 bandwidth_bytes_per_s,
    u64 base_latency_ns,
    u64 policy_penalty_ns,
    bool bidirectional
) {
    if (!registry || registry->link_count >= AI_DIST_MAX_LINKS) return NULL;
    if (!valid_transport(transport)) return NULL;
    if (src_endpoint_id == dst_endpoint_id || bandwidth_bytes_per_s == 0) return NULL;
    if (endpoint_index(registry, src_endpoint_id) < 0 ||
        endpoint_index(registry, dst_endpoint_id) < 0) return NULL;

    ai_dist_link *link = &registry->links[registry->link_count++];
    link->id = registry->next_link_id++;
    link->src_endpoint_id = src_endpoint_id;
    link->dst_endpoint_id = dst_endpoint_id;
    link->transport = transport;
    link->bandwidth_bytes_per_s = bandwidth_bytes_per_s;
    link->base_latency_ns = base_latency_ns;
    link->policy_penalty_ns = policy_penalty_ns;
    link->bidirectional = bidirectional;
    link->health = AI_DIST_HEALTH_HEALTHY;
    return link;
}

ai_remote_tensor *ai_dist_add_remote_tensor(
    ai_dist_registry *registry,
    const char *name,
    u64 origin_tensor_id,
    u64 owner_endpoint_id,
    u64 bytes,
    u64 generation,
    u32 flags
) {
    if (!registry || registry->remote_tensor_count >= AI_DIST_MAX_REMOTE_TENSORS) return NULL;
    if (bytes == 0 || endpoint_index(registry, owner_endpoint_id) < 0) return NULL;

    ai_remote_tensor *tensor = &registry->remote_tensors[registry->remote_tensor_count++];
    tensor->id = registry->next_remote_tensor_id++;
    tensor->name = name;
    tensor->origin_tensor_id = origin_tensor_id;
    tensor->owner_endpoint_id = owner_endpoint_id;
    tensor->bytes = bytes;
    tensor->generation = generation;
    tensor->flags = flags;
    tensor->replica_count = 0;
    tensor->health = AI_DIST_HEALTH_HEALTHY;
    for (u32 i = 0; i < AI_DIST_MAX_REPLICAS; ++i) tensor->replica_endpoint_ids[i] = 0;
    return tensor;
}

ai_remote_tensor *ai_dist_publish_tensor(
    ai_dist_registry *registry,
    const char *name,
    const ai_tensor *tensor,
    u64 owner_endpoint_id,
    u64 generation,
    u32 flags
) {
    if (!tensor) return NULL;
    return ai_dist_add_remote_tensor(
        registry,
        name ? name : tensor->name,
        tensor->id,
        owner_endpoint_id,
        tensor->bytes,
        generation,
        flags
    );
}

bool ai_dist_tensor_add_replica(
    ai_dist_registry *registry,
    u64 remote_tensor_id,
    u64 endpoint_id
) {
    if (!registry || endpoint_index(registry, endpoint_id) < 0) return false;
    i32 idx = remote_tensor_index(registry, remote_tensor_id);
    if (idx < 0) return false;

    ai_remote_tensor *tensor = &registry->remote_tensors[(u32)idx];
    if (tensor->owner_endpoint_id == endpoint_id) return true;

    for (u32 i = 0; i < tensor->replica_count; ++i) {
        if (tensor->replica_endpoint_ids[i] == endpoint_id) return true;
    }
    if (tensor->replica_count >= AI_DIST_MAX_REPLICAS) return false;

    tensor->replica_endpoint_ids[tensor->replica_count++] = endpoint_id;
    tensor->flags |= AI_REMOTE_TENSOR_REPLICATED;
    return true;
}

ai_dist_collective *ai_dist_add_collective(
    ai_dist_registry *registry,
    const char *name,
    ai_collective_kind kind,
    const u64 *participant_endpoint_ids,
    u32 participant_count,
    u64 root_endpoint_id,
    u64 bytes
) {
    if (!registry || !participant_endpoint_ids) return NULL;
    if (registry->collective_count >= AI_DIST_MAX_COLLECTIVES) return NULL;
    if (!valid_collective_kind(kind)) return NULL;
    if (participant_count < 2 || participant_count > AI_DIST_MAX_PARTICIPANTS) return NULL;

    bool root_is_participant = false;
    for (u32 i = 0; i < participant_count; ++i) {
        if (endpoint_index(registry, participant_endpoint_ids[i]) < 0) return NULL;
        if (participant_endpoint_ids[i] == root_endpoint_id) root_is_participant = true;
        for (u32 j = 0; j < i; ++j) {
            if (participant_endpoint_ids[j] == participant_endpoint_ids[i]) return NULL;
        }
    }

    if (kind == AI_COLLECTIVE_BROADCAST || kind == AI_COLLECTIVE_REDUCE) {
        if (root_endpoint_id == 0 || !root_is_participant) return NULL;
    } else if (root_endpoint_id != 0 && !root_is_participant) {
        return NULL;
    }

    ai_dist_collective *collective = &registry->collectives[registry->collective_count++];
    collective->id = registry->next_collective_id++;
    collective->name = name;
    collective->kind = kind;
    collective->participant_count = participant_count;
    collective->root_endpoint_id = root_endpoint_id;
    collective->bytes = bytes;
    collective->algorithm = AI_COLLECTIVE_ALGO_TREE;
    collective->health = AI_DIST_HEALTH_HEALTHY;

    for (u32 i = 0; i < AI_DIST_MAX_PARTICIPANTS; ++i) {
        collective->participant_endpoint_ids[i] = i < participant_count ? participant_endpoint_ids[i] : 0;
    }

    if ((kind == AI_COLLECTIVE_ALLREDUCE ||
         kind == AI_COLLECTIVE_ALLGATHER ||
         kind == AI_COLLECTIVE_REDUCE_SCATTER) && participant_count >= 4) {
        collective->algorithm = AI_COLLECTIVE_ALGO_RING;
    }

    return collective;
}

const ai_dist_endpoint *ai_dist_get_endpoint(const ai_dist_registry *registry, u64 endpoint_id) {
    i32 idx = endpoint_index(registry, endpoint_id);
    return idx < 0 ? NULL : &registry->endpoints[(u32)idx];
}

ai_dist_endpoint *ai_dist_get_endpoint_mut(ai_dist_registry *registry, u64 endpoint_id) {
    i32 idx = endpoint_index(registry, endpoint_id);
    return idx < 0 ? NULL : &registry->endpoints[(u32)idx];
}

const ai_dist_link *ai_dist_get_link(const ai_dist_registry *registry, u64 link_id) {
    i32 idx = link_index(registry, link_id);
    return idx < 0 ? NULL : &registry->links[(u32)idx];
}

const ai_remote_tensor *ai_dist_get_remote_tensor(const ai_dist_registry *registry, u64 remote_tensor_id) {
    i32 idx = remote_tensor_index(registry, remote_tensor_id);
    return idx < 0 ? NULL : &registry->remote_tensors[(u32)idx];
}

ai_remote_tensor *ai_dist_get_remote_tensor_mut(ai_dist_registry *registry, u64 remote_tensor_id) {
    i32 idx = remote_tensor_index(registry, remote_tensor_id);
    return idx < 0 ? NULL : &registry->remote_tensors[(u32)idx];
}

const ai_dist_collective *ai_dist_get_collective(const ai_dist_registry *registry, u64 collective_id) {
    i32 idx = collective_index(registry, collective_id);
    return idx < 0 ? NULL : &registry->collectives[(u32)idx];
}

bool ai_dist_set_endpoint_health(ai_dist_registry *registry, u64 endpoint_id, ai_dist_health health) {
    if (!valid_health(health)) return false;
    ai_dist_endpoint *endpoint = ai_dist_get_endpoint_mut(registry, endpoint_id);
    if (!endpoint) return false;
    endpoint->health = health;
    return true;
}

bool ai_dist_set_link_health(ai_dist_registry *registry, u64 link_id, ai_dist_health health) {
    if (!registry || !valid_health(health)) return false;
    i32 idx = link_index(registry, link_id);
    if (idx < 0) return false;
    registry->links[(u32)idx].health = health;
    return true;
}

bool ai_dist_update_endpoint_load(
    ai_dist_registry *registry,
    u64 endpoint_id,
    u64 memory_free_bytes,
    u64 queue_delay_ns
) {
    ai_dist_endpoint *endpoint = ai_dist_get_endpoint_mut(registry, endpoint_id);
    if (!endpoint || memory_free_bytes > endpoint->memory_total_bytes) return false;
    endpoint->memory_free_bytes = memory_free_bytes;
    endpoint->queue_delay_ns = queue_delay_ns;
    return true;
}

u64 ai_dist_estimate_link_ns(const ai_dist_link *link, u64 bytes) {
    if (!link || link->health == AI_DIST_HEALTH_DOWN || link->bandwidth_bytes_per_s == 0) {
        return AI_DIST_INF_NS;
    }

    u64 transfer_ns = mul_div_ceil_u64(bytes, 1000000000ull, link->bandwidth_bytes_per_s);
    u64 total = sat_add(link->base_latency_ns, link->policy_penalty_ns);
    return sat_add(total, transfer_ns);
}

bool ai_dist_find_path(
    ai_dist_registry *registry,
    u64 src_endpoint_id,
    u64 dst_endpoint_id,
    u64 bytes,
    u32 transfer_flags,
    ai_dist_path *out_path
) {
    if (!registry || !out_path) return false;

    out_path->valid = false;
    out_path->src_endpoint_id = src_endpoint_id;
    out_path->dst_endpoint_id = dst_endpoint_id;
    out_path->hop_count = 0;
    out_path->estimated_ns = AI_DIST_INF_NS;
    out_path->bottleneck_bandwidth_bytes_per_s = 0;
    for (u32 i = 0; i < AI_DIST_MAX_PATH_HOPS + 1; ++i) out_path->endpoint_ids[i] = 0;
    for (u32 i = 0; i < AI_DIST_MAX_PATH_HOPS; ++i) out_path->link_ids[i] = 0;

    i32 src_idx = endpoint_index(registry, src_endpoint_id);
    i32 dst_idx = endpoint_index(registry, dst_endpoint_id);
    if (src_idx < 0 || dst_idx < 0) {
        registry->metrics.path_failures++;
        return false;
    }

    if (registry->endpoints[(u32)src_idx].health == AI_DIST_HEALTH_DOWN ||
        registry->endpoints[(u32)dst_idx].health == AI_DIST_HEALTH_DOWN) {
        registry->metrics.path_failures++;
        return false;
    }

    if (src_endpoint_id == dst_endpoint_id) {
        out_path->valid = true;
        out_path->endpoint_ids[0] = src_endpoint_id;
        out_path->estimated_ns = 0;
        out_path->bottleneck_bandwidth_bytes_per_s = ~0ull;
        return true;
    }

    const u32 n = registry->endpoint_count;
    u64 dist[AI_DIST_MAX_ENDPOINTS];
    i32 prev_endpoint[AI_DIST_MAX_ENDPOINTS];
    i32 prev_link[AI_DIST_MAX_ENDPOINTS];
    bool visited[AI_DIST_MAX_ENDPOINTS];

    for (u32 i = 0; i < n; ++i) {
        dist[i] = AI_DIST_INF_NS;
        prev_endpoint[i] = -1;
        prev_link[i] = -1;
        visited[i] = false;
    }
    dist[(u32)src_idx] = 0;

    for (u32 step = 0; step < n; ++step) {
        i32 current = -1;
        u64 best = AI_DIST_INF_NS;

        for (u32 i = 0; i < n; ++i) {
            if (!visited[i] && dist[i] < best) {
                best = dist[i];
                current = (i32)i;
            }
        }
        if (current < 0) break;
        if (current == dst_idx) break;

        visited[(u32)current] = true;
        const ai_dist_endpoint *cur_endpoint = &registry->endpoints[(u32)current];
        if (cur_endpoint->health == AI_DIST_HEALTH_DOWN) continue;

        for (u32 li = 0; li < registry->link_count; ++li) {
            const ai_dist_link *link = &registry->links[li];
            if (link->health == AI_DIST_HEALTH_DOWN || !link_transport_allowed(link, transfer_flags)) continue;

            u64 neighbor_id = 0;
            if (link->src_endpoint_id == cur_endpoint->id) {
                neighbor_id = link->dst_endpoint_id;
            } else if (link->bidirectional && link->dst_endpoint_id == cur_endpoint->id) {
                neighbor_id = link->src_endpoint_id;
            } else {
                continue;
            }

            i32 neighbor = endpoint_index(registry, neighbor_id);
            if (neighbor < 0 || visited[(u32)neighbor]) continue;
            if (registry->endpoints[(u32)neighbor].health == AI_DIST_HEALTH_DOWN) continue;

            bool staging_allowed = (transfer_flags & AI_DIST_TRANSFER_ALLOW_STAGING) != 0;
            bool direct_only = (transfer_flags & AI_DIST_TRANSFER_REQUIRE_DIRECT) != 0 ||
                               !staging_allowed;
            if (direct_only && cur_endpoint->id != src_endpoint_id) {
                continue;
            }

            u64 edge_cost = link_policy_cost(link, transfer_flags, bytes);
            if (edge_cost >= AI_DIST_INF_NS) continue;
            if (registry->endpoints[(u32)neighbor].health == AI_DIST_HEALTH_DEGRADED) {
                edge_cost = sat_add(edge_cost, 1000000ull);
            }

            u64 candidate = sat_add(dist[(u32)current], edge_cost);
            if (candidate < dist[(u32)neighbor]) {
                dist[(u32)neighbor] = candidate;
                prev_endpoint[(u32)neighbor] = current;
                prev_link[(u32)neighbor] = (i32)li;
            }
        }
    }

    if (dist[(u32)dst_idx] >= AI_DIST_INF_NS) {
        registry->metrics.path_failures++;
        return false;
    }

    u32 reverse_nodes[AI_DIST_MAX_PATH_HOPS + 1];
    u32 reverse_links[AI_DIST_MAX_PATH_HOPS];
    u32 hop_count = 0;
    i32 walk = dst_idx;

    while (walk != src_idx) {
        if (hop_count >= AI_DIST_MAX_PATH_HOPS) {
            registry->metrics.path_failures++;
            return false;
        }
        i32 p = prev_endpoint[(u32)walk];
        i32 l = prev_link[(u32)walk];
        if (p < 0 || l < 0) {
            registry->metrics.path_failures++;
            return false;
        }
        reverse_nodes[hop_count] = (u32)walk;
        reverse_links[hop_count] = (u32)l;
        walk = p;
        hop_count++;
    }
    reverse_nodes[hop_count] = (u32)src_idx;

    out_path->endpoint_ids[0] = src_endpoint_id;
    out_path->bottleneck_bandwidth_bytes_per_s = ~0ull;

    for (u32 i = 0; i < hop_count; ++i) {
        u32 rev = hop_count - 1u - i;
        const ai_dist_link *link = &registry->links[reverse_links[rev]];
        const ai_dist_endpoint *endpoint = &registry->endpoints[reverse_nodes[rev]];

        if (!link_allows_direction(link, out_path->endpoint_ids[i], endpoint->id)) {
            registry->metrics.path_failures++;
            return false;
        }

        out_path->link_ids[i] = link->id;
        out_path->endpoint_ids[i + 1] = endpoint->id;
        if (link->bandwidth_bytes_per_s < out_path->bottleneck_bandwidth_bytes_per_s) {
            out_path->bottleneck_bandwidth_bytes_per_s = link->bandwidth_bytes_per_s;
        }
    }

    out_path->hop_count = hop_count;
    out_path->estimated_ns = dist[(u32)dst_idx];
    out_path->valid = true;
    return true;
}

bool ai_dist_plan_transfer(
    ai_dist_registry *registry,
    const ai_dist_transfer_request *request,
    ai_dist_transfer_plan *out_plan
) {
    if (!registry || !request || !out_plan || request->bytes == 0) return false;

    out_plan->feasible = false;
    out_plan->request = *request;
    out_plan->estimated_ns = AI_DIST_INF_NS;
    out_plan->deadline_miss = false;

    if (request->remote_tensor_id != 0) {
        const ai_remote_tensor *tensor = ai_dist_get_remote_tensor(registry, request->remote_tensor_id);
        if (!tensor || tensor->health == AI_DIST_HEALTH_DOWN || request->bytes > tensor->bytes) return false;

        bool source_has_tensor = tensor->owner_endpoint_id == request->src_endpoint_id;
        for (u32 i = 0; !source_has_tensor && i < tensor->replica_count; ++i) {
            if (tensor->replica_endpoint_ids[i] == request->src_endpoint_id) source_has_tensor = true;
        }
        if (!source_has_tensor) return false;
    }

    if (!ai_dist_find_path(
            registry,
            request->src_endpoint_id,
            request->dst_endpoint_id,
            request->bytes,
            request->flags,
            &out_plan->path)) {
        return false;
    }

    if ((request->flags & AI_DIST_TRANSFER_REQUIRE_DIRECT) && out_plan->path.hop_count > 1) {
        registry->metrics.path_failures++;
        return false;
    }

    out_plan->estimated_ns = out_plan->path.estimated_ns;
    out_plan->feasible = true;
    out_plan->deadline_miss = request->deadline_budget_ns > 0 &&
                              out_plan->estimated_ns > request->deadline_budget_ns;

    registry->metrics.transfers_planned++;
    if (out_plan->deadline_miss) registry->metrics.deadline_misses_predicted++;
    return true;
}

static u64 memory_pressure_penalty(const ai_dist_endpoint *endpoint, u64 requested_bytes) {
    if (!endpoint || requested_bytes > endpoint->memory_free_bytes) return AI_DIST_INF_NS;
    if (endpoint->memory_total_bytes == 0) return AI_DIST_INF_NS;

    u64 remaining = endpoint->memory_free_bytes - requested_bytes;
    u64 percent_remaining = mul_div_ceil_u64(remaining, 100ull, endpoint->memory_total_bytes);

    if (percent_remaining >= 25) return 0;
    if (percent_remaining >= 15) return 250000ull;   /* 0.25 ms */
    if (percent_remaining >= 10) return 1000000ull;  /* 1 ms */
    if (percent_remaining >= 5)  return 5000000ull;  /* 5 ms */
    return 20000000ull;                              /* 20 ms */
}

bool ai_dist_place(
    ai_dist_registry *registry,
    const ai_dist_placement_request *request,
    ai_dist_placement_result *out_result
) {
    if (!registry || !request || !out_result) return false;
    registry->metrics.placements_attempted++;

    out_result->feasible = false;
    out_result->endpoint_id = 0;
    out_result->transfer_ns = 0;
    out_result->queue_ns = 0;
    out_result->execution_ns = 0;
    out_result->memory_pressure_penalty_ns = 0;
    out_result->locality_penalty_ns = 0;
    out_result->deadline_penalty_ns = 0;
    out_result->total_score_ns = AI_DIST_INF_NS;
    out_result->deadline_miss = false;

    for (u32 i = 0; i < registry->endpoint_count; ++i) {
        const ai_dist_endpoint *endpoint = &registry->endpoints[i];

        if (endpoint->health == AI_DIST_HEALTH_DOWN) continue;
        if ((endpoint->device_kind & request->required_device_kind) == 0) continue;
        if (request->required_memory_bytes > endpoint->memory_free_bytes) continue;
        if (endpoint->compute_units_per_s == 0) continue;

        u64 transfer_ns = 0;
        if (request->data_endpoint_id != 0 && request->data_endpoint_id != endpoint->id &&
            request->input_bytes > 0) {
            ai_dist_path path;
            if (!ai_dist_find_path(
                    registry,
                    request->data_endpoint_id,
                    endpoint->id,
                    request->input_bytes,
                    AI_DIST_TRANSFER_ALLOW_STAGING | AI_DIST_TRANSFER_PREFER_RDMA,
                    &path)) {
                continue;
            }
            transfer_ns = path.estimated_ns;
        }

        u64 execution_ns = request->compute_units == 0
            ? 0
            : mul_div_ceil_u64(request->compute_units, 1000000000ull, endpoint->compute_units_per_s);

        u64 memory_penalty = memory_pressure_penalty(endpoint, request->required_memory_bytes);
        if (memory_penalty >= AI_DIST_INF_NS) continue;

        u64 locality_penalty = (request->prefer_local && !endpoint->local) ? 1000000ull : 0;
        u64 queue_ns = endpoint->queue_delay_ns;
        if (endpoint->health == AI_DIST_HEALTH_DEGRADED) queue_ns = sat_add(queue_ns, 1000000ull);

        u64 score = transfer_ns;
        score = sat_add(score, queue_ns);
        score = sat_add(score, execution_ns);
        score = sat_add(score, memory_penalty);
        score = sat_add(score, locality_penalty);

        bool deadline_miss = request->deadline_budget_ns > 0 && score > request->deadline_budget_ns;
        u64 deadline_penalty = 0;
        if (deadline_miss) {
            deadline_penalty = sat_add(score - request->deadline_budget_ns, 1000000ull);
            score = sat_add(score, deadline_penalty);
        }

        if (!out_result->feasible || score < out_result->total_score_ns ||
            (score == out_result->total_score_ns && endpoint->local)) {
            out_result->feasible = true;
            out_result->endpoint_id = endpoint->id;
            out_result->transfer_ns = transfer_ns;
            out_result->queue_ns = queue_ns;
            out_result->execution_ns = execution_ns;
            out_result->memory_pressure_penalty_ns = memory_penalty;
            out_result->locality_penalty_ns = locality_penalty;
            out_result->deadline_penalty_ns = deadline_penalty;
            out_result->total_score_ns = score;
            out_result->deadline_miss = deadline_miss;
        }
    }

    if (out_result->feasible) {
        registry->metrics.placements_succeeded++;
        if (out_result->deadline_miss) registry->metrics.deadline_misses_predicted++;
        return true;
    }
    return false;
}

static u32 ceil_log2_u32(u32 n) {
    if (n <= 1) return 0;
    u32 p = 1;
    u32 rounds = 0;
    while (p < n) {
        p <<= 1;
        rounds++;
    }
    return rounds;
}

static bool collective_participants_healthy(
    const ai_dist_registry *registry,
    const ai_dist_collective *collective
) {
    for (u32 i = 0; i < collective->participant_count; ++i) {
        const ai_dist_endpoint *endpoint = ai_dist_get_endpoint(
            registry,
            collective->participant_endpoint_ids[i]
        );
        if (!endpoint || endpoint->health == AI_DIST_HEALTH_DOWN) return false;
    }
    return true;
}

static bool ring_critical_path(
    ai_dist_registry *registry,
    const ai_dist_collective *collective,
    u64 bytes_per_round,
    u64 *out_critical_ns
) {
    u64 critical = 0;
    for (u32 i = 0; i < collective->participant_count; ++i) {
        u32 next = (i + 1u) % collective->participant_count;
        ai_dist_path path;
        if (!ai_dist_find_path(
                registry,
                collective->participant_endpoint_ids[i],
                collective->participant_endpoint_ids[next],
                bytes_per_round,
                AI_DIST_TRANSFER_ALLOW_STAGING | AI_DIST_TRANSFER_PREFER_RDMA,
                &path)) {
            return false;
        }
        if (path.estimated_ns > critical) critical = path.estimated_ns;
    }
    *out_critical_ns = critical;
    return true;
}

static bool tree_critical_path(
    ai_dist_registry *registry,
    const ai_dist_collective *collective,
    u64 bytes_per_round,
    u64 *out_critical_ns
) {
    u64 root = collective->root_endpoint_id;
    if (root == 0) root = collective->participant_endpoint_ids[0];

    bool need_outbound = collective->kind != AI_COLLECTIVE_REDUCE;
    bool need_inbound = collective->kind != AI_COLLECTIVE_BROADCAST;

    u64 critical = 0;
    for (u32 i = 0; i < collective->participant_count; ++i) {
        u64 peer = collective->participant_endpoint_ids[i];
        if (peer == root) continue;

        if (need_outbound) {
            ai_dist_path outbound;
            if (!ai_dist_find_path(
                    registry,
                    root,
                    peer,
                    bytes_per_round,
                    AI_DIST_TRANSFER_ALLOW_STAGING | AI_DIST_TRANSFER_PREFER_RDMA,
                    &outbound)) {
                return false;
            }
            if (outbound.estimated_ns > critical) critical = outbound.estimated_ns;
        }

        if (need_inbound) {
            ai_dist_path inbound;
            if (!ai_dist_find_path(
                    registry,
                    peer,
                    root,
                    bytes_per_round,
                    AI_DIST_TRANSFER_ALLOW_STAGING | AI_DIST_TRANSFER_PREFER_RDMA,
                    &inbound)) {
                return false;
            }
            if (inbound.estimated_ns > critical) critical = inbound.estimated_ns;
        }
    }
    *out_critical_ns = critical;
    return true;
}

bool ai_dist_plan_collective(
    ai_dist_registry *registry,
    u64 collective_id,
    ai_dist_collective_plan *out_plan
) {
    if (!registry || !out_plan) return false;
    i32 idx = collective_index(registry, collective_id);
    if (idx < 0) return false;

    ai_dist_collective *collective = &registry->collectives[(u32)idx];
    out_plan->feasible = false;
    out_plan->collective_id = collective_id;
    out_plan->algorithm = collective->algorithm;
    out_plan->estimated_ns = AI_DIST_INF_NS;
    out_plan->communication_rounds = 0;
    out_plan->critical_path_ns = AI_DIST_INF_NS;

    if (collective->health == AI_DIST_HEALTH_DOWN ||
        !collective_participants_healthy(registry, collective)) return false;

    u32 n = collective->participant_count;
    u32 rounds = 0;
    u64 bytes_per_round = collective->bytes;

    if (collective->kind == AI_COLLECTIVE_BARRIER) {
        collective->algorithm = AI_COLLECTIVE_ALGO_TREE;
        rounds = 2u * ceil_log2_u32(n);
        bytes_per_round = 8;
    } else if (collective->algorithm == AI_COLLECTIVE_ALGO_RING) {
        if (collective->kind == AI_COLLECTIVE_ALLREDUCE) rounds = 2u * (n - 1u);
        else rounds = n - 1u;
        bytes_per_round = collective->bytes == 0 ? 1 : (collective->bytes + n - 1u) / n;
    } else {
        rounds = ceil_log2_u32(n);
        if (collective->kind == AI_COLLECTIVE_ALLREDUCE) rounds *= 2u;
        if (collective->kind == AI_COLLECTIVE_REDUCE_SCATTER) rounds = ceil_log2_u32(n);
    }

    u64 critical = 0;
    bool ok = collective->algorithm == AI_COLLECTIVE_ALGO_RING
        ? ring_critical_path(registry, collective, bytes_per_round, &critical)
        : tree_critical_path(registry, collective, bytes_per_round, &critical);

    if (!ok) return false;

    out_plan->feasible = true;
    out_plan->algorithm = collective->algorithm;
    out_plan->communication_rounds = rounds;
    out_plan->critical_path_ns = critical;
    out_plan->estimated_ns = mul_div_ceil_u64(critical, rounds, 1);
    registry->metrics.collectives_planned++;
    return true;
}

bool ai_dist_validate(const ai_dist_registry *registry) {
    if (!registry) return false;
    if (registry->endpoint_count > AI_DIST_MAX_ENDPOINTS ||
        registry->link_count > AI_DIST_MAX_LINKS ||
        registry->remote_tensor_count > AI_DIST_MAX_REMOTE_TENSORS ||
        registry->collective_count > AI_DIST_MAX_COLLECTIVES) return false;

    u64 max_endpoint_id = 0;
    u64 max_link_id = 0;
    u64 max_tensor_id = 0;
    u64 max_collective_id = 0;

    for (u32 i = 0; i < registry->endpoint_count; ++i) {
        const ai_dist_endpoint *e = &registry->endpoints[i];
        if (e->id == 0 || e->device_kind == 0 || e->compute_units_per_s == 0) return false;
        if (!valid_health(e->health)) return false;
        if (e->memory_free_bytes > e->memory_total_bytes) return false;
        for (u32 j = 0; j < i; ++j) if (registry->endpoints[j].id == e->id) return false;
        if (e->id > max_endpoint_id) max_endpoint_id = e->id;
    }

    for (u32 i = 0; i < registry->link_count; ++i) {
        const ai_dist_link *l = &registry->links[i];
        if (l->id == 0 || l->bandwidth_bytes_per_s == 0) return false;
        if (!valid_transport(l->transport) || !valid_health(l->health)) return false;
        if (l->src_endpoint_id == l->dst_endpoint_id) return false;
        if (endpoint_index(registry, l->src_endpoint_id) < 0 ||
            endpoint_index(registry, l->dst_endpoint_id) < 0) return false;
        for (u32 j = 0; j < i; ++j) if (registry->links[j].id == l->id) return false;
        if (l->id > max_link_id) max_link_id = l->id;
    }

    for (u32 i = 0; i < registry->remote_tensor_count; ++i) {
        const ai_remote_tensor *t = &registry->remote_tensors[i];
        if (t->id == 0 || t->bytes == 0 || !valid_health(t->health)) return false;
        if (endpoint_index(registry, t->owner_endpoint_id) < 0) return false;
        if (t->replica_count > AI_DIST_MAX_REPLICAS) return false;
        for (u32 r = 0; r < t->replica_count; ++r) {
            if (endpoint_index(registry, t->replica_endpoint_ids[r]) < 0) return false;
            if (t->replica_endpoint_ids[r] == t->owner_endpoint_id) return false;
            for (u32 q = 0; q < r; ++q) {
                if (t->replica_endpoint_ids[q] == t->replica_endpoint_ids[r]) return false;
            }
        }
        if (t->replica_count > 0 && (t->flags & AI_REMOTE_TENSOR_REPLICATED) == 0) return false;
        for (u32 j = 0; j < i; ++j) if (registry->remote_tensors[j].id == t->id) return false;
        if (t->id > max_tensor_id) max_tensor_id = t->id;
    }

    for (u32 i = 0; i < registry->collective_count; ++i) {
        const ai_dist_collective *c = &registry->collectives[i];
        if (c->id == 0 || c->participant_count < 2 ||
            c->participant_count > AI_DIST_MAX_PARTICIPANTS) return false;
        if (!valid_collective_kind(c->kind) ||
            !valid_collective_algorithm(c->algorithm) ||
            !valid_health(c->health)) return false;

        bool root_is_participant = false;
        for (u32 p = 0; p < c->participant_count; ++p) {
            if (endpoint_index(registry, c->participant_endpoint_ids[p]) < 0) return false;
            if (c->participant_endpoint_ids[p] == c->root_endpoint_id) root_is_participant = true;
            for (u32 q = 0; q < p; ++q) {
                if (c->participant_endpoint_ids[q] == c->participant_endpoint_ids[p]) return false;
            }
        }
        if (c->kind == AI_COLLECTIVE_BROADCAST || c->kind == AI_COLLECTIVE_REDUCE) {
            if (c->root_endpoint_id == 0 || !root_is_participant) return false;
        } else if (c->root_endpoint_id != 0 && !root_is_participant) {
            return false;
        }
        for (u32 j = 0; j < i; ++j) if (registry->collectives[j].id == c->id) return false;
        if (c->id > max_collective_id) max_collective_id = c->id;
    }

    if (registry->next_endpoint_id == 0 || registry->next_endpoint_id <= max_endpoint_id) return false;
    if (registry->next_link_id == 0 || registry->next_link_id <= max_link_id) return false;
    if (registry->next_remote_tensor_id == 0 || registry->next_remote_tensor_id <= max_tensor_id) return false;
    if (registry->next_collective_id == 0 || registry->next_collective_id <= max_collective_id) return false;

    return true;
}

const ai_dist_metrics *ai_dist_get_metrics(const ai_dist_registry *registry) {
    return registry ? &registry->metrics : NULL;
}

const char *ai_dist_health_name(ai_dist_health health) {
    switch (health) {
        case AI_DIST_HEALTH_HEALTHY: return "HEALTHY";
        case AI_DIST_HEALTH_DEGRADED: return "DEGRADED";
        case AI_DIST_HEALTH_DOWN: return "DOWN";
        default: return "UNKNOWN";
    }
}

const char *ai_dist_transport_name(ai_dist_transport transport) {
    switch (transport) {
        case AI_DIST_TRANSPORT_SIMULATED: return "SIMULATED";
        case AI_DIST_TRANSPORT_SHM: return "SHM";
        case AI_DIST_TRANSPORT_TCP: return "TCP";
        case AI_DIST_TRANSPORT_RDMA: return "RDMA";
        case AI_DIST_TRANSPORT_CXL_FABRIC: return "CXL_FABRIC";
        default: return "UNKNOWN";
    }
}

const char *ai_collective_kind_name(ai_collective_kind kind) {
    switch (kind) {
        case AI_COLLECTIVE_BROADCAST: return "BROADCAST";
        case AI_COLLECTIVE_REDUCE: return "REDUCE";
        case AI_COLLECTIVE_ALLREDUCE: return "ALLREDUCE";
        case AI_COLLECTIVE_ALLGATHER: return "ALLGATHER";
        case AI_COLLECTIVE_REDUCE_SCATTER: return "REDUCE_SCATTER";
        case AI_COLLECTIVE_BARRIER: return "BARRIER";
        default: return "UNKNOWN";
    }
}

const char *ai_collective_algorithm_name(ai_collective_algorithm algorithm) {
    switch (algorithm) {
        case AI_COLLECTIVE_ALGO_TREE: return "TREE";
        case AI_COLLECTIVE_ALGO_RING: return "RING";
        default: return "UNKNOWN";
    }
}
