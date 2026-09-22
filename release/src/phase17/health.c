#include "nexora/phase17/health.h"

#include <stdatomic.h>
#include <string.h>

typedef struct nx_health_slot {
    atomic_uint in_use;
    char name[NX_HEALTH_NAME_MAX + 1u];
    atomic_uint state;
    atomic_ullong checks;
    atomic_ullong failures;
    atomic_ullong last_update_ns;
    atomic_int last_code;
} nx_health_slot_t;

static nx_health_slot_t g_slots[NX_HEALTH_MAX_COMPONENTS];
static atomic_uint g_count = 0u;

static size_t bounded_len(const char *s, size_t limit) {
    size_t n = 0u;
    while (n < limit && s[n] != '\0') ++n;
    return n;
}

void nx_health_reset(void) {
    for (uint32_t i = 0u; i < NX_HEALTH_MAX_COMPONENTS; ++i) {
        g_slots[i].name[0] = '\0';
        atomic_store_explicit(&g_slots[i].in_use, 0u, memory_order_relaxed);
        atomic_store_explicit(&g_slots[i].state, (unsigned)NX_HEALTH_UNKNOWN, memory_order_relaxed);
        atomic_store_explicit(&g_slots[i].checks, 0u, memory_order_relaxed);
        atomic_store_explicit(&g_slots[i].failures, 0u, memory_order_relaxed);
        atomic_store_explicit(&g_slots[i].last_update_ns, 0u, memory_order_relaxed);
        atomic_store_explicit(&g_slots[i].last_code, 0, memory_order_relaxed);
    }
    atomic_store_explicit(&g_count, 0u, memory_order_release);
}

int nx_health_register(const char *name, uint32_t *out_id) {
    if (!name || !*name || !out_id) return -1;
    const size_t n = bounded_len(name, NX_HEALTH_NAME_MAX + 1u);
    if (n > NX_HEALTH_NAME_MAX) return -2;

    const uint32_t id = atomic_fetch_add_explicit(&g_count, 1u, memory_order_acq_rel);
    if (id >= NX_HEALTH_MAX_COMPONENTS) {
        atomic_fetch_sub_explicit(&g_count, 1u, memory_order_acq_rel);
        return -3;
    }

    nx_health_slot_t *slot = &g_slots[id];
    memcpy(slot->name, name, n);
    slot->name[n] = '\0';
    atomic_store_explicit(&slot->state, (unsigned)NX_HEALTH_UNKNOWN, memory_order_relaxed);
    atomic_store_explicit(&slot->checks, 0u, memory_order_relaxed);
    atomic_store_explicit(&slot->failures, 0u, memory_order_relaxed);
    atomic_store_explicit(&slot->last_update_ns, 0u, memory_order_relaxed);
    atomic_store_explicit(&slot->last_code, 0, memory_order_relaxed);
    atomic_store_explicit(&slot->in_use, 1u, memory_order_release);
    *out_id = id;
    return 0;
}

int nx_health_update(uint32_t id, nx_health_state_t state, int32_t code, uint64_t now_ns) {
    if (id >= NX_HEALTH_MAX_COMPONENTS) return -1;
    if ((unsigned)state > (unsigned)NX_HEALTH_FAILED) return -2;
    nx_health_slot_t *slot = &g_slots[id];
    if (!atomic_load_explicit(&slot->in_use, memory_order_acquire)) return -3;

    atomic_fetch_add_explicit(&slot->checks, 1u, memory_order_relaxed);
    if (state == NX_HEALTH_FAILED) {
        atomic_fetch_add_explicit(&slot->failures, 1u, memory_order_relaxed);
    }
    atomic_store_explicit(&slot->last_code, code, memory_order_relaxed);
    atomic_store_explicit(&slot->last_update_ns, now_ns, memory_order_relaxed);
    atomic_store_explicit(&slot->state, (unsigned)state, memory_order_release);
    return 0;
}

int nx_health_get(uint32_t id, nx_health_snapshot_t *out) {
    if (!out || id >= NX_HEALTH_MAX_COMPONENTS) return -1;
    nx_health_slot_t *slot = &g_slots[id];
    if (!atomic_load_explicit(&slot->in_use, memory_order_acquire)) return -2;

    memset(out, 0, sizeof(*out));
    out->id = id;
    memcpy(out->name, slot->name, sizeof(out->name));
    out->state = (nx_health_state_t)atomic_load_explicit(&slot->state, memory_order_acquire);
    out->checks = atomic_load_explicit(&slot->checks, memory_order_relaxed);
    out->failures = atomic_load_explicit(&slot->failures, memory_order_relaxed);
    out->last_update_ns = atomic_load_explicit(&slot->last_update_ns, memory_order_relaxed);
    out->last_code = atomic_load_explicit(&slot->last_code, memory_order_relaxed);
    return 0;
}

size_t nx_health_snapshot_all(nx_health_snapshot_t *out, size_t capacity) {
    if (!out || capacity == 0u) return 0u;
    uint32_t count = atomic_load_explicit(&g_count, memory_order_acquire);
    if (count > NX_HEALTH_MAX_COMPONENTS) count = NX_HEALTH_MAX_COMPONENTS;
    size_t written = 0u;
    for (uint32_t i = 0u; i < count && written < capacity; ++i) {
        if (nx_health_get(i, &out[written]) == 0) ++written;
    }
    return written;
}

nx_health_summary_t nx_health_summarize(void) {
    nx_health_summary_t s;
    memset(&s, 0, sizeof(s));
    s.aggregate = NX_HEALTH_UNKNOWN;

    uint32_t count = atomic_load_explicit(&g_count, memory_order_acquire);
    if (count > NX_HEALTH_MAX_COMPONENTS) count = NX_HEALTH_MAX_COMPONENTS;
    for (uint32_t i = 0u; i < count; ++i) {
        nx_health_slot_t *slot = &g_slots[i];
        if (!atomic_load_explicit(&slot->in_use, memory_order_acquire)) continue;
        ++s.total;
        nx_health_state_t st = (nx_health_state_t)atomic_load_explicit(&slot->state, memory_order_acquire);
        switch (st) {
            case NX_HEALTH_OK: ++s.ok; break;
            case NX_HEALTH_DEGRADED: ++s.degraded; break;
            case NX_HEALTH_FAILED: ++s.failed; break;
            default: ++s.unknown; break;
        }
    }
    if (s.failed) s.aggregate = NX_HEALTH_FAILED;
    else if (s.degraded) s.aggregate = NX_HEALTH_DEGRADED;
    else if (s.total && s.ok == s.total) s.aggregate = NX_HEALTH_OK;
    return s;
}

const char *nx_health_state_string(nx_health_state_t state) {
    switch (state) {
        case NX_HEALTH_OK: return "ok";
        case NX_HEALTH_DEGRADED: return "degraded";
        case NX_HEALTH_FAILED: return "failed";
        default: return "unknown";
    }
}
