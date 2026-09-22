#ifndef NEXORA_PHASE17_WATCHDOG_H
#define NEXORA_PHASE17_WATCHDOG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NX_WATCHDOG_MAX_TARGETS 64u

typedef struct nx_watchdog_target {
    uint32_t component_id;
    uint64_t timeout_ns;
    uint64_t last_heartbeat_ns;
    uint64_t misses;
    uint8_t armed;
    uint8_t overdue;
} nx_watchdog_target_t;

void nx_watchdog_reset(void);
int nx_watchdog_arm(uint32_t component_id, uint64_t timeout_ns, uint64_t now_ns);
int nx_watchdog_disarm(uint32_t component_id);
int nx_watchdog_heartbeat(uint32_t component_id, uint64_t now_ns);
size_t nx_watchdog_check(uint64_t now_ns, nx_watchdog_target_t *overdue, size_t capacity);
int nx_watchdog_get(uint32_t component_id, nx_watchdog_target_t *out);

#ifdef __cplusplus
}
#endif

#endif
