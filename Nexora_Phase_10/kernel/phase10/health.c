#include "nexora/phase10/health.h"

static nx_component_health_t *find_slot(nx_health_registry_t *registry,
                                        nx_p10_id_t id)
{
    size_t i;
    if (!registry) return NULL;
    for (i = 0; i < NX_P10_MAX_COMPONENTS; ++i) {
        if (registry->entries[i].in_use && registry->entries[i].id == id)
            return &registry->entries[i];
    }
    return NULL;
}

void nx_health_init(nx_health_registry_t *registry, nx_health_policy_t policy)
{
    if (!registry) return;
    nx_p10_memzero(registry, sizeof(*registry));
    registry->policy = policy;
}

nx_p10_status_t nx_health_register(nx_health_registry_t *registry,
                                    nx_p10_id_t id,
                                    nx_component_kind_t kind,
                                    nx_p10_time_t now)
{
    size_t i;
    nx_component_health_t *existing;
    if (!registry || id == 0) return NX_P10_EINVAL;
    existing = find_slot(registry, id);
    if (existing) return NX_P10_ESTATE;
    for (i = 0; i < NX_P10_MAX_COMPONENTS; ++i) {
        if (!registry->entries[i].in_use) {
            registry->entries[i].id = id;
            registry->entries[i].kind = kind;
            registry->entries[i].state = NX_HEALTH_HEALTHY;
            registry->entries[i].last_heartbeat = now;
            registry->entries[i].generation = 1;
            registry->entries[i].in_use = 1;
            return NX_P10_OK;
        }
    }
    return NX_P10_ENOSPC;
}

nx_p10_status_t nx_health_heartbeat(nx_health_registry_t *registry,
                                     nx_p10_id_t id,
                                     nx_p10_time_t now,
                                     int success)
{
    nx_component_health_t *entry = find_slot(registry, id);
    nx_health_state_t before;
    if (!entry) return NX_P10_ENOENT;
    before = entry->state;
    entry->last_heartbeat = now;
    if (success) {
        entry->consecutive_failures = 0;
        if (entry->state != NX_HEALTH_QUARANTINED &&
            entry->state != NX_HEALTH_RECOVERING)
            entry->state = NX_HEALTH_HEALTHY;
    } else {
        ++entry->consecutive_failures;
        if (registry->policy.max_consecutive_failures != 0u &&
            entry->consecutive_failures >= registry->policy.max_consecutive_failures)
            entry->state = NX_HEALTH_FAILED;
        else if (entry->state != NX_HEALTH_QUARANTINED)
            entry->state = NX_HEALTH_DEGRADED;
    }
    if (entry->state != before)
        ++entry->generation;
    return NX_P10_OK;
}

nx_p10_status_t nx_health_set_state(nx_health_registry_t *registry,
                                     nx_p10_id_t id,
                                     nx_health_state_t state)
{
    nx_component_health_t *entry = find_slot(registry, id);
    if (!entry) return NX_P10_ENOENT;
    if (entry->state != state) {
        entry->state = state;
        ++entry->generation;
    }
    return NX_P10_OK;
}

nx_component_health_t *nx_health_get(nx_health_registry_t *registry,
                                     nx_p10_id_t id)
{
    return find_slot(registry, id);
}

size_t nx_health_scan(nx_health_registry_t *registry, nx_p10_time_t now)
{
    size_t i, changed = 0;
    if (!registry) return 0;
    for (i = 0; i < NX_P10_MAX_COMPONENTS; ++i) {
        nx_component_health_t *e = &registry->entries[i];
        nx_p10_time_t age;
        nx_health_state_t next;
        if (!e->in_use || e->state == NX_HEALTH_QUARANTINED) continue;
        age = now >= e->last_heartbeat ? now - e->last_heartbeat : 0;
        next = e->state;
        if (registry->policy.fail_after && age >= registry->policy.fail_after)
            next = NX_HEALTH_FAILED;
        else if (registry->policy.degrade_after && age >= registry->policy.degrade_after &&
                 e->state == NX_HEALTH_HEALTHY)
            next = NX_HEALTH_DEGRADED;
        if (next != e->state) {
            e->state = next;
            ++e->generation;
            ++changed;
        }
    }
    return changed;
}
