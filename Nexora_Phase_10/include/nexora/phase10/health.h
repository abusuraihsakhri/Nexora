#ifndef NEXORA_PHASE10_HEALTH_H
#define NEXORA_PHASE10_HEALTH_H

#include "p10_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nx_health_state {
    NX_HEALTH_UNKNOWN = 0,
    NX_HEALTH_HEALTHY,
    NX_HEALTH_DEGRADED,
    NX_HEALTH_FAILED,
    NX_HEALTH_RECOVERING,
    NX_HEALTH_QUARANTINED
} nx_health_state_t;

typedef enum nx_component_kind {
    NX_COMPONENT_KERNEL = 0,
    NX_COMPONENT_SCHEDULER,
    NX_COMPONENT_MEMORY,
    NX_COMPONENT_ACCELERATOR,
    NX_COMPONENT_RUNTIME,
    NX_COMPONENT_NETWORK,
    NX_COMPONENT_DISTRIBUTED_NODE,
    NX_COMPONENT_SERVICE
} nx_component_kind_t;

typedef struct nx_health_policy {
    nx_p10_time_t degrade_after;
    nx_p10_time_t fail_after;
    uint32_t max_consecutive_failures;
} nx_health_policy_t;

typedef struct nx_component_health {
    nx_p10_id_t id;
    nx_component_kind_t kind;
    nx_health_state_t state;
    nx_p10_time_t last_heartbeat;
    uint32_t consecutive_failures;
    uint32_t recovery_attempts;
    uint32_t generation;
    uint8_t in_use;
} nx_component_health_t;

typedef struct nx_health_registry {
    nx_component_health_t entries[NX_P10_MAX_COMPONENTS];
    nx_health_policy_t policy;
} nx_health_registry_t;

void nx_health_init(nx_health_registry_t *registry, nx_health_policy_t policy);
nx_p10_status_t nx_health_register(nx_health_registry_t *registry,
                                    nx_p10_id_t id,
                                    nx_component_kind_t kind,
                                    nx_p10_time_t now);
nx_p10_status_t nx_health_heartbeat(nx_health_registry_t *registry,
                                     nx_p10_id_t id,
                                     nx_p10_time_t now,
                                     int success);
nx_p10_status_t nx_health_set_state(nx_health_registry_t *registry,
                                     nx_p10_id_t id,
                                     nx_health_state_t state);
nx_component_health_t *nx_health_get(nx_health_registry_t *registry,
                                     nx_p10_id_t id);
size_t nx_health_scan(nx_health_registry_t *registry, nx_p10_time_t now);

#ifdef __cplusplus
}
#endif

#endif
