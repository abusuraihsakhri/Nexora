#include "nexora/phase17/trace.h"

#include <stdatomic.h>

typedef struct nx_trace_slot {
    atomic_flag writer_lock;
    atomic_ullong published_sequence;
    atomic_ullong timestamp_ns;
    atomic_uint component_id;
    atomic_uint event_id;
    atomic_uint severity;
    atomic_uint flags;
    atomic_ullong arg0;
    atomic_ullong arg1;
} nx_trace_slot_t;

static nx_trace_slot_t g_trace[NX_TRACE_CAPACITY];
static atomic_ullong g_cursor = 0u;

void nx_trace_reset(void) {
    for (size_t i = 0u; i < NX_TRACE_CAPACITY; ++i) {
        atomic_flag_clear_explicit(&g_trace[i].writer_lock, memory_order_relaxed);
        atomic_store_explicit(&g_trace[i].published_sequence, 0u, memory_order_relaxed);
        atomic_store_explicit(&g_trace[i].timestamp_ns, 0u, memory_order_relaxed);
        atomic_store_explicit(&g_trace[i].component_id, 0u, memory_order_relaxed);
        atomic_store_explicit(&g_trace[i].event_id, 0u, memory_order_relaxed);
        atomic_store_explicit(&g_trace[i].severity, 0u, memory_order_relaxed);
        atomic_store_explicit(&g_trace[i].flags, 0u, memory_order_relaxed);
        atomic_store_explicit(&g_trace[i].arg0, 0u, memory_order_relaxed);
        atomic_store_explicit(&g_trace[i].arg1, 0u, memory_order_relaxed);
    }
    atomic_store_explicit(&g_cursor, 0u, memory_order_release);
}

uint64_t nx_trace_emit(uint64_t timestamp_ns,
                       uint32_t component_id,
                       uint16_t event_id,
                       nx_trace_severity_t severity,
                       uint8_t flags,
                       uint64_t arg0,
                       uint64_t arg1) {
    const uint64_t seq = atomic_fetch_add_explicit(&g_cursor, 1u, memory_order_acq_rel) + 1u;
    nx_trace_slot_t *slot = &g_trace[(seq - 1u) % NX_TRACE_CAPACITY];

    while (atomic_flag_test_and_set_explicit(&slot->writer_lock, memory_order_acquire)) {
        /* Collision can occur only after ring wrap; keep the hot path allocation-free. */
    }
    /* Mark the slot unpublished while its payload is replaced. */
    atomic_store_explicit(&slot->published_sequence, 0u, memory_order_release);
    atomic_store_explicit(&slot->timestamp_ns, timestamp_ns, memory_order_relaxed);
    atomic_store_explicit(&slot->component_id, component_id, memory_order_relaxed);
    atomic_store_explicit(&slot->event_id, (unsigned)event_id, memory_order_relaxed);
    atomic_store_explicit(&slot->severity, (unsigned)severity, memory_order_relaxed);
    atomic_store_explicit(&slot->flags, (unsigned)flags, memory_order_relaxed);
    atomic_store_explicit(&slot->arg0, arg0, memory_order_relaxed);
    atomic_store_explicit(&slot->arg1, arg1, memory_order_relaxed);
    atomic_store_explicit(&slot->published_sequence, seq, memory_order_release);
    atomic_flag_clear_explicit(&slot->writer_lock, memory_order_release);
    return seq;
}

uint64_t nx_trace_count(void) {
    return atomic_load_explicit(&g_cursor, memory_order_acquire);
}

static int copy_sequence(uint64_t seq, nx_trace_event_t *out) {
    nx_trace_slot_t *slot = &g_trace[(seq - 1u) % NX_TRACE_CAPACITY];
    const uint64_t before = atomic_load_explicit(&slot->published_sequence, memory_order_acquire);
    if (before != seq) return 0;

    nx_trace_event_t ev;
    ev.sequence = seq;
    ev.timestamp_ns = atomic_load_explicit(&slot->timestamp_ns, memory_order_relaxed);
    ev.component_id = atomic_load_explicit(&slot->component_id, memory_order_relaxed);
    ev.event_id = (uint16_t)atomic_load_explicit(&slot->event_id, memory_order_relaxed);
    ev.severity = (uint8_t)atomic_load_explicit(&slot->severity, memory_order_relaxed);
    ev.flags = (uint8_t)atomic_load_explicit(&slot->flags, memory_order_relaxed);
    ev.arg0 = atomic_load_explicit(&slot->arg0, memory_order_relaxed);
    ev.arg1 = atomic_load_explicit(&slot->arg1, memory_order_relaxed);

    atomic_thread_fence(memory_order_acquire);
    if (atomic_load_explicit(&slot->published_sequence, memory_order_acquire) != seq) return 0;
    *out = ev;
    return 1;
}

size_t nx_trace_copy_latest(nx_trace_event_t *out, size_t capacity) {
    if (!out || capacity == 0u) return 0u;
    const uint64_t total = atomic_load_explicit(&g_cursor, memory_order_acquire);
    const uint64_t available = total < NX_TRACE_CAPACITY ? total : NX_TRACE_CAPACITY;
    const uint64_t wanted = capacity < available ? capacity : available;
    const uint64_t first_seq = total - wanted + 1u;
    size_t written = 0u;

    for (uint64_t seq = first_seq; seq <= total; ++seq) {
        nx_trace_event_t ev;
        if (copy_sequence(seq, &ev)) out[written++] = ev;
    }
    return written;
}

static uint64_t fnv1a_u64(uint64_t h, uint64_t v) {
    for (unsigned i = 0; i < 8u; ++i) {
        h ^= (uint8_t)(v & 0xffu);
        h *= 1099511628211ull;
        v >>= 8u;
    }
    return h;
}

uint64_t nx_trace_checksum_latest(size_t max_events) {
    uint64_t h = 1469598103934665603ull;
    if (max_events == 0u) return h;
    if (max_events > NX_TRACE_CAPACITY) max_events = NX_TRACE_CAPACITY;

    const uint64_t total = atomic_load_explicit(&g_cursor, memory_order_acquire);
    const uint64_t available = total < NX_TRACE_CAPACITY ? total : NX_TRACE_CAPACITY;
    const uint64_t wanted = max_events < available ? max_events : available;
    const uint64_t first_seq = total - wanted + 1u;

    for (uint64_t seq = first_seq; seq <= total; ++seq) {
        nx_trace_event_t ev;
        if (!copy_sequence(seq, &ev)) continue;
        h = fnv1a_u64(h, ev.sequence);
        h = fnv1a_u64(h, ev.timestamp_ns);
        h = fnv1a_u64(h, ev.component_id);
        h = fnv1a_u64(h, ev.event_id);
        h = fnv1a_u64(h, ev.severity);
        h = fnv1a_u64(h, ev.flags);
        h = fnv1a_u64(h, ev.arg0);
        h = fnv1a_u64(h, ev.arg1);
    }
    return h;
}
