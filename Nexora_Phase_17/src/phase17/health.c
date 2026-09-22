#include "nexora/phase17/health.h"

#include <stdatomic.h>

typedef struct nx_health_slot {
    atomic_uint in_use;
    atomic_uint version;
    char name[NX_HEALTH_NAME_MAX + 1u];
    atomic_uint state;
    atomic_ullong checks;
    atomic_ullong failures;
    atomic_ullong last_update_ns;
    atomic_int last_code;
} nx_health_slot_t;

static nx_health_slot_t g_slots[NX_HEALTH_MAX_COMPONENTS];
static atomic_uint g_count = 0u;
static atomic_flag g_registry_lock = ATOMIC_FLAG_INIT;

static void registry_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_registry_lock, memory_order_acquire)) {
    }
}

static void registry_unlock(void) {
    atomic_flag_clear_explicit(&g_registry_lock, memory_order_release);
}

static size_t bounded_len(const char *s, size_t limit) {
    size_t n = 0u;
    while (n < limit && s[n] != '\0') ++n;
    return n;
}

static int same_name(const char *a, const char *b) {
    size_t i = 0u;
    for (;;) {
        if (a[i] != b[i]) return 0;
        if (a[i] == '\0') return 1;
        ++i;
    }
}

static void zero_snapshot(nx_health_snapshot_t *out) {
    unsigned char *p = (unsigned char *)out;
    for (size_t i = 0u; i < sizeof(*out); ++i) p[i] = 0u;
}

void nx_health_reset(void) {
    registry_lock();
    for (uint32_t i = 0u; i < NX_HEALTH_MAX_COMPONENTS; ++i) {
        g_slots[i].name[0] = '\0';
        atomic_store_explicit(&g_slots[i].version, 0u, memory_order_relaxed);
        atomic_store_explicit(&g_slots[i].state, (unsigned)NX_HEALTH_UNKNOWN, memory_order_relaxed);
        atomic_store_explicit(&g_slots[i].checks, 0u, memory_order_relaxed);
        atomic_store_explicit(&g_slots[i].failures, 0u, memory_order_relaxed);
        atomic_store_explicit(&g_slots[i].last_update_ns, 0u, memory_order_relaxed);
        atomic_store_explicit(&g_slots[i].last_code, 0, memory_order_relaxed);
        atomic_store_explicit(&g_slots[i].in_use, 0u, memory_order_release);
    }
    atomic_store_explicit(&g_count, 0u, memory_order_release);
    registry_unlock();
}

int nx_health_register(const char *name, uint32_t *out_id) {
    if (!name || !*name || !out_id) return -1;
    const size_t n = bounded_len(name, NX_HEALTH_NAME_MAX + 1u);
    if (n > NX_HEALTH_NAME_MAX) return -2;

    registry_lock();

    uint32_t free_id = NX_HEALTH_MAX_COMPONENTS;
    for (uint32_t i = 0u; i < NX_HEALTH_MAX_COMPONENTS; ++i) {
        if (atomic_load_explicit(&g_slots[i].in_use, memory_order_acquire)) {
            if (same_name(g_slots[i].name, name)) {
                registry_unlock();
                return -4;
            }
        } else if (free_id == NX_HEALTH_MAX_COMPONENTS) {
            free_id = i;
        }
    }

    if (free_id == NX_HEALTH_MAX_COMPONENTS) {
        registry_unlock();
        return -3;
    }

    nx_health_slot_t *slot = &g_slots[free_id];
    for (size_t i = 0u; i < n; ++i) slot->name[i] = name[i];
    slot->name[n] = '\0';
    atomic_store_explicit(&slot->version, 0u, memory_order_relaxed);
    atomic_store_explicit(&slot->state, (unsigned)NX_HEALTH_UNKNOWN, memory_order_relaxed);
    atomic_store_explicit(&slot->checks, 0u, memory_order_relaxed);
    atomic_store_explicit(&slot->failures, 0u, memory_order_relaxed);
    atomic_store_explicit(&slot->last_update_ns, 0u, memory_order_relaxed);
    atomic_store_explicit(&slot->last_code, 0, memory_order_relaxed);
    atomic_store_explicit(&slot->in_use, 1u, memory_order_release);
    atomic_fetch_add_explicit(&g_count, 1u, memory_order_release);
    *out_id = free_id;

    registry_unlock();
    return 0;
}

int nx_health_update(uint32_t id, nx_health_state_t state, int32_t code, uint64_t now_ns) {
    if (id >= NX_HEALTH_MAX_COMPONENTS) return -1;
    if ((unsigned)state > (unsigned)NX_HEALTH_FAILED) return -2;

    nx_health_slot_t *slot = &g_slots[id];
    if (!atomic_load_explicit(&slot->in_use, memory_order_acquire)) return -3;

    atomic_fetch_add_explicit(&slot->version, 1u, memory_order_acq_rel);
    atomic_fetch_add_explicit(&slot->checks, 1u, memory_order_relaxed);
    if (state == NX_HEALTH_FAILED) {
        atomic_fetch_add_explicit(&slot->failures, 1u, memory_order_relaxed);
    }
    atomic_store_explicit(&slot->last_code, code, memory_order_relaxed);
    atomic_store_explicit(&slot->last_update_ns, now_ns, memory_order_relaxed);
    atomic_store_explicit(&slot->state, (unsigned)state, memory_order_relaxed);
    atomic_fetch_add_explicit(&slot->version, 1u, memory_order_release);
    return 0;
}

int nx_health_get(uint32_t id, nx_health_snapshot_t *out) {
    if (!out || id >= NX_HEALTH_MAX_COMPONENTS) return -1;
    nx_health_slot_t *slot = &g_slots[id];
    if (!atomic_load_explicit(&slot->in_use, memory_order_acquire)) return -2;

    for (unsigned attempt = 0u; attempt < 32u; ++attempt) {
        const unsigned before = atomic_load_explicit(&slot->version, memory_order_acquire);
        if (before & 1u) continue;

        zero_snapshot(out);
        out->id = id;
        for (size_t i = 0u; i < sizeof(out->name); ++i) out->name[i] = slot->name[i];
        out->state = (nx_health_state_t)atomic_load_explicit(&slot->state, memory_order_relaxed);
        out->checks = atomic_load_explicit(&slot->checks, memory_order_relaxed);
        out->failures = atomic_load_explicit(&slot->failures, memory_order_relaxed);
        out->last_update_ns = atomic_load_explicit(&slot->last_update_ns, memory_order_relaxed);
        out->last_code = atomic_load_explicit(&slot->last_code, memory_order_relaxed);

        atomic_thread_fence(memory_order_acquire);
        const unsigned after = atomic_load_explicit(&slot->version, memory_order_acquire);
        if (before == after && !(after & 1u)) return 0;
    }

    return -3;
}

size_t nx_health_snapshot_all(nx_health_snapshot_t *out, size_t capacity) {
    if (!out || capacity == 0u) return 0u;
    size_t written = 0u;
    for (uint32_t i = 0u; i < NX_HEALTH_MAX_COMPONENTS && written < capacity; ++i) {
        if (nx_health_get(i, &out[written]) == 0) ++written;
    }
    return written;
}

nx_health_summary_t nx_health_summarize(void) {
    nx_health_summary_t s = {0};
    s.aggregate = NX_HEALTH_UNKNOWN;

    for (uint32_t i = 0u; i < NX_HEALTH_MAX_COMPONENTS; ++i) {
        if (!atomic_load_explicit(&g_slots[i].in_use, memory_order_acquire)) continue;
        nx_health_snapshot_t snap;
        if (nx_health_get(i, &snap) != 0) continue;
        ++s.total;
        switch (snap.state) {
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
