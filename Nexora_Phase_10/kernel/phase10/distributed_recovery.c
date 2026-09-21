#include "nexora/phase10/distributed_recovery.h"

void nx_node_table_init(nx_node_table_t *table)
{
    if (!table) return;
    nx_p10_memzero(table, sizeof(*table));
}

nx_p10_status_t nx_node_upsert(nx_node_table_t *table, const nx_node_health_t *node)
{
    size_t i, free_index = NX_P10_MAX_NODES;
    if (!table || !node || node->node_id == 0) return NX_P10_EINVAL;
    for (i = 0; i < NX_P10_MAX_NODES; ++i) {
        if (table->nodes[i].in_use && table->nodes[i].node_id == node->node_id) {
            table->nodes[i] = *node;
            table->nodes[i].in_use = 1;
            return NX_P10_OK;
        }
        if (!table->nodes[i].in_use && free_index == NX_P10_MAX_NODES)
            free_index = i;
    }
    if (free_index == NX_P10_MAX_NODES) return NX_P10_ENOSPC;
    table->nodes[free_index] = *node;
    table->nodes[free_index].in_use = 1;
    return NX_P10_OK;
}

uint64_t nx_node_recovery_score(const nx_node_health_t *node)
{
    if (!node || !node->in_use || !node->reachable || node->quarantined)
        return UINT64_MAX;
    return (uint64_t)node->load_milli * 4u +
           (uint64_t)node->memory_pressure_milli * 3u +
           (uint64_t)node->accelerator_pressure_milli * 5u +
           (uint64_t)node->link_cost_milli * 2u;
}

const nx_node_health_t *nx_node_select_recovery_target(const nx_node_table_t *table,
                                                        nx_p10_id_t exclude_node_id)
{
    const nx_node_health_t *best = NULL;
    uint64_t best_score = UINT64_MAX;
    size_t i;
    if (!table) return NULL;
    for (i = 0; i < NX_P10_MAX_NODES; ++i) {
        const nx_node_health_t *node = &table->nodes[i];
        uint64_t score;
        if (!node->in_use || node->node_id == exclude_node_id) continue;
        score = nx_node_recovery_score(node);
        if (score < best_score ||
            (score == best_score && score != UINT64_MAX &&
             (!best || node->node_id < best->node_id))) {
            best_score = score;
            best = node;
        }
    }
    return best;
}
