#ifndef NEXORA_PHASE10_WATCHDOG_H
#define NEXORA_PHASE10_WATCHDOG_H

#include "recovery.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nx_watchdog {
    nx_recovery_engine_t *recovery;
    nx_failure_class_t default_failure;
} nx_watchdog_t;

void nx_watchdog_init(nx_watchdog_t *watchdog,
                      nx_recovery_engine_t *recovery,
                      nx_failure_class_t default_failure);
size_t nx_watchdog_tick(nx_watchdog_t *watchdog,
                        const nx_security_context_t *security,
                        nx_p10_time_t now);

#ifdef __cplusplus
}
#endif

#endif
