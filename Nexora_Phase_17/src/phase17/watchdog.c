#include "nexora/phase17/watchdog.h"

#include <stdatomic.h>

static nx_watchdog_target_t g_targets[NX_WATCHDOG_MAX_TARGETS];
static atomic_flag g_watchdog_lock = ATOMIC_FLAG_INIT;

static void lock_watchdog(void) {
    while (atomic_flag_test_and_set_explicit(&g_watchdog_lock, memory_order_acquire)) {
    }
}

static void unlock_watchdog(void) {
    atomic_flag_clear_explicit(&g_watchdog_lock, memory_order_release);
}

static void clear_target(nx_watchdog_target_t *target) {
    unsigned char *p = (unsigned char *)target;
    for (size_t i = 0u; i < sizeof(*target); ++i) p[i] = 0u;
}

static int find_target_locked(uint32_t component_id) {
    for (uint32_t i = 0u; i < NX_WATCHDOG_MAX_TARGETS; ++i) {
        if (g_targets[i].armed && g_targets[i].component_id == component_id) return (int)i;
    }
    return -1;
}

void nx_watchdog_reset(void) {
    lock_watchdog();
    for (uint32_t i = 0u; i < NX_WATCHDOG_MAX_TARGETS; ++i) clear_target(&g_targets[i]);
    unlock_watchdog();
}

int nx_watchdog_arm(uint32_t component_id, uint64_t timeout_ns, uint64_t now_ns) {
    if (timeout_ns == 0u) return -1;

    lock_watchdog();
    int idx = find_target_locked(component_id);
    if (idx < 0) {
        for (uint32_t i = 0u; i < NX_WATCHDOG_MAX_TARGETS; ++i) {
            if (!g_targets[i].armed) {
                idx = (int)i;
                break;
            }
        }
    }
    if (idx < 0) {
        unlock_watchdog();
        return -2;
    }

    g_targets[idx].component_id = component_id;
    g_targets[idx].timeout_ns = timeout_ns;
    g_targets[idx].last_heartbeat_ns = now_ns;
    g_targets[idx].misses = 0u;
    g_targets[idx].armed = 1u;
    g_targets[idx].overdue = 0u;
    unlock_watchdog();
    return 0;
}

int nx_watchdog_disarm(uint32_t component_id) {
    lock_watchdog();
    int idx = find_target_locked(component_id);
    if (idx < 0) {
        unlock_watchdog();
        return -1;
    }
    clear_target(&g_targets[idx]);
    unlock_watchdog();
    return 0;
}

int nx_watchdog_heartbeat(uint32_t component_id, uint64_t now_ns) {
    lock_watchdog();
    int idx = find_target_locked(component_id);
    if (idx < 0) {
        unlock_watchdog();
        return -1;
    }
    g_targets[idx].last_heartbeat_ns = now_ns;
    g_targets[idx].overdue = 0u;
    unlock_watchdog();
    return 0;
}

size_t nx_watchdog_check(uint64_t now_ns, nx_watchdog_target_t *overdue, size_t capacity) {
    size_t n = 0u;
    lock_watchdog();

    for (uint32_t i = 0u; i < NX_WATCHDOG_MAX_TARGETS; ++i) {
        nx_watchdog_target_t *t = &g_targets[i];
        if (!t->armed) continue;

        const uint64_t elapsed =
            now_ns >= t->last_heartbeat_ns ? now_ns - t->last_heartbeat_ns : 0u;
        const uint8_t is_overdue = elapsed >= t->timeout_ns ? 1u : 0u;
        if (is_overdue && !t->overdue) ++t->misses;
        t->overdue = is_overdue;

        if (is_overdue && overdue && n < capacity) overdue[n++] = *t;
    }

    unlock_watchdog();
    return n;
}

int nx_watchdog_get(uint32_t component_id, nx_watchdog_target_t *out) {
    if (!out) return -1;

    lock_watchdog();
    int idx = find_target_locked(component_id);
    if (idx < 0) {
        unlock_watchdog();
        return -2;
    }
    *out = g_targets[idx];
    unlock_watchdog();
    return 0;
}
