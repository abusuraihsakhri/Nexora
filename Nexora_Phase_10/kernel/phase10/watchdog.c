#include "nexora/phase10/watchdog.h"

void nx_watchdog_init(nx_watchdog_t *watchdog,
                      nx_recovery_engine_t *recovery,
                      nx_failure_class_t default_failure)
{
    if (!watchdog) return;
    watchdog->recovery = recovery;
    watchdog->default_failure = default_failure;
}

size_t nx_watchdog_tick(nx_watchdog_t *watchdog,
                        const nx_security_context_t *security,
                        nx_p10_time_t now)
{
    size_t i, actions = 0;
    nx_health_registry_t *registry;
    if (!watchdog || !watchdog->recovery || !watchdog->recovery->health)
        return 0;
    registry = watchdog->recovery->health;
    (void)nx_health_scan(registry, now);
    for (i = 0; i < NX_P10_MAX_COMPONENTS; ++i) {
        nx_component_health_t *e = &registry->entries[i];
        nx_recovery_decision_t d;
        if (!e->in_use || e->state != NX_HEALTH_FAILED) continue;
        d = nx_recovery_decide(watchdog->recovery, e->id, watchdog->default_failure);
        if (d.action == NX_RECOVERY_NONE) continue;
        (void)nx_recovery_execute(watchdog->recovery, security, now, d);
        ++actions;
    }
    return actions;
}
