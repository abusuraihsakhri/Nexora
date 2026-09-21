#ifndef NEXORA_PHASE10_DISTRIBUTED_RECOVERY_H
#define NEXORA_PHASE10_DISTRIBUTED_RECOVERY_H

#include "p10_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nx_node_health {
    nx_p10_id_t node_id;
    uint32_t load_milli;
    uint32_t memory_pressure_milli;
    uint32_t accelerator_pressure_milli;
    uint32_t link_cost_milli;
    uint8_t reachable;
    uint8_t quarantined;
    uint8_t in_use;
} nx_node_health_t;

typedef struct nx_node_table {
    nx_node_health_t nodes[NX_P10_MAX_NODES];
} nx_node_table_t;

void nx_node_table_init(nx_node_table_t *table);
nx_p10_status_t nx_node_upsert(nx_node_table_t *table, const nx_node_health_t *node);
const nx_node_health_t *nx_node_select_recovery_target(const nx_node_table_t *table,
                                                        nx_p10_id_t exclude_node_id);
uint64_t nx_node_recovery_score(const nx_node_health_t *node);

#ifdef __cplusplus
}
#endif

#endif
