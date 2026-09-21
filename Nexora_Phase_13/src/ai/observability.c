#include <ai/observability.h>

#define AI_OBS_U64_MAX (~0ull)

static u64 obs_sat_add(u64 a, u64 b) {
    if (AI_OBS_U64_MAX - a < b) {
        return AI_OBS_U64_MAX;
    }
    return a + b;
}

static bool obs_valid_visibility(ai_obs_visibility v) {
    return v == AI_OBS_VIS_PUBLIC || v == AI_OBS_VIS_OPERATOR || v == AI_OBS_VIS_SENSITIVE;
}

static bool obs_valid_metric_kind(ai_obs_metric_kind kind) {
    return kind == AI_OBS_METRIC_COUNTER || kind == AI_OBS_METRIC_GAUGE || kind == AI_OBS_METRIC_HISTOGRAM;
}

static bool obs_valid_unit(ai_obs_unit unit) {
    return unit >= AI_OBS_UNIT_NONE && unit <= AI_OBS_UNIT_PERCENT_MILLI;
}

static bool obs_valid_scope_kind(ai_obs_scope_kind kind) {
    return kind >= AI_OBS_SCOPE_SYSTEM && kind <= AI_OBS_SCOPE_SERVICE;
}

static bool obs_valid_severity(ai_obs_severity severity) {
    return severity >= AI_OBS_SEV_DEBUG && severity <= AI_OBS_SEV_FATAL;
}

static bool obs_valid_trace_kind(ai_obs_trace_kind kind) {
    return kind >= AI_OBS_TRACE_BOOT && kind <= AI_OBS_TRACE_CUSTOM;
}

static bool obs_valid_span_kind(ai_obs_span_kind kind) {
    return kind >= AI_OBS_SPAN_WORK && kind <= AI_OBS_SPAN_AGENT;
}

static bool obs_valid_hist_bounds(const u64 *bounds, u32 count) {
    u32 i;
    if (count == 0u || count > AI_OBS_MAX_HIST_BOUNDS || bounds == NULL) {
        return false;
    }
    for (i = 1u; i < count; ++i) {
        if (bounds[i] <= bounds[i - 1u]) {
            return false;
        }
    }
    return true;
}

static void obs_zero_metric(ai_obs_metric *m) {
    u32 i;
    m->id = 0u;
    m->key = 0u;
    m->kind = AI_OBS_METRIC_COUNTER;
    m->unit = AI_OBS_UNIT_NONE;
    m->scope_kind = AI_OBS_SCOPE_SYSTEM;
    m->visibility = AI_OBS_VIS_PUBLIC;
    m->scope_id = 0ull;
    m->value = 0ull;
    m->updates = 0ull;
    m->last_update_ns = 0ull;
    m->hist_count = 0ull;
    m->hist_sum = 0ull;
    m->hist_min = 0ull;
    m->hist_max = 0ull;
    m->hist_bound_count = 0u;
    for (i = 0u; i < AI_OBS_MAX_HIST_BOUNDS; ++i) {
        m->hist_bounds[i] = 0ull;
    }
    for (i = 0u; i < AI_OBS_MAX_HIST_BOUNDS + 1u; ++i) {
        m->hist_buckets[i] = 0ull;
    }
}

static void obs_zero_trace(ai_obs_trace_event *e) {
    e->sequence = 0ull;
    e->timestamp_ns = 0ull;
    e->kind = AI_OBS_TRACE_BOOT;
    e->severity = AI_OBS_SEV_INFO;
    e->visibility = AI_OBS_VIS_PUBLIC;
    e->span_id = 0ull;
    e->correlation_id = 0ull;
    e->actor_id = 0ull;
    e->object_id = 0ull;
    e->arg0 = 0ull;
    e->arg1 = 0ull;
}

static void obs_zero_span(ai_obs_span *span) {
    span->id = 0ull;
    span->kind = AI_OBS_SPAN_WORK;
    span->visibility = AI_OBS_VIS_PUBLIC;
    span->correlation_id = 0ull;
    span->actor_id = 0ull;
    span->object_id = 0ull;
    span->start_ns = 0ull;
    span->active = false;
}

static ai_obs_metric *obs_metric_find(ai_observability *obs, u32 metric_id) {
    u32 i;
    if (obs == NULL || metric_id == AI_OBS_INVALID_METRIC) {
        return NULL;
    }
    for (i = 0u; i < obs->metric_registry.count; ++i) {
        if (obs->metric_registry.metrics[i].id == metric_id) {
            return &obs->metric_registry.metrics[i];
        }
    }
    return NULL;
}

const ai_obs_metric *ai_obs_metric_find_const(const ai_observability *obs, u32 metric_id) {
    u32 i;
    if (obs == NULL || metric_id == AI_OBS_INVALID_METRIC) {
        return NULL;
    }
    for (i = 0u; i < obs->metric_registry.count; ++i) {
        if (obs->metric_registry.metrics[i].id == metric_id) {
            return &obs->metric_registry.metrics[i];
        }
    }
    return NULL;
}

u64 ai_obs_trace_append(
    ai_observability *obs,
    ai_obs_trace_kind kind,
    ai_obs_severity severity,
    ai_obs_visibility visibility,
    u64 span_id,
    u64 correlation_id,
    u64 actor_id,
    u64 object_id,
    u64 arg0,
    u64 arg1,
    u64 now_ns
) {
    ai_obs_trace_event *slot;
    u32 index;

    if (obs == NULL || !obs_valid_visibility(visibility) ||
        !obs_valid_trace_kind(kind) || !obs_valid_severity(severity)) {
        return 0ull;
    }

    if (obs->trace.count < AI_OBS_TRACE_CAPACITY) {
        index = (obs->trace.head + obs->trace.count) % AI_OBS_TRACE_CAPACITY;
        obs->trace.count++;
    } else {
        index = obs->trace.head;
        obs->trace.head = (obs->trace.head + 1u) % AI_OBS_TRACE_CAPACITY;
        obs->trace.dropped_events++;
    }

    slot = &obs->trace.events[index];
    slot->sequence = obs->trace.next_sequence++;
    slot->timestamp_ns = now_ns;
    slot->kind = kind;
    slot->severity = severity;
    slot->visibility = visibility;
    slot->span_id = span_id;
    slot->correlation_id = correlation_id;
    slot->actor_id = actor_id;
    slot->object_id = object_id;
    slot->arg0 = arg0;
    slot->arg1 = arg1;
    return slot->sequence;
}

void ai_obs_init(ai_observability *obs, u64 now_ns) {
    u32 i;
    if (obs == NULL) {
        return;
    }

    obs->metric_registry.count = 0u;
    obs->metric_registry.next_id = 1u;
    for (i = 0u; i < AI_OBS_MAX_METRICS; ++i) {
        obs_zero_metric(&obs->metric_registry.metrics[i]);
    }

    obs->trace.head = 0u;
    obs->trace.count = 0u;
    obs->trace.next_sequence = 1ull;
    obs->trace.dropped_events = 0ull;
    for (i = 0u; i < AI_OBS_TRACE_CAPACITY; ++i) {
        obs_zero_trace(&obs->trace.events[i]);
    }

    obs->span_registry.active_count = 0u;
    obs->span_registry.peak_active = 0u;
    obs->span_registry.next_id = 1ull;
    for (i = 0u; i < AI_OBS_MAX_ACTIVE_SPANS; ++i) {
        obs_zero_span(&obs->span_registry.spans[i]);
    }

    obs->next_snapshot_id = 1ull;
    obs->rejected_metric_updates = 0ull;
    obs->rejected_spans = 0ull;
    obs->clock_regressions = 0ull;

    ai_obs_trace_append(
        obs,
        AI_OBS_TRACE_BOOT,
        AI_OBS_SEV_INFO,
        AI_OBS_VIS_PUBLIC,
        0ull,
        0ull,
        0ull,
        0ull,
        0ull,
        0ull,
        now_ns
    );
}

u32 ai_obs_metric_register(
    ai_observability *obs,
    u32 key,
    ai_obs_metric_kind kind,
    ai_obs_unit unit,
    ai_obs_scope_kind scope_kind,
    u64 scope_id,
    ai_obs_visibility visibility,
    const u64 *hist_bounds,
    u32 hist_bound_count,
    u64 now_ns
) {
    ai_obs_metric *m;
    u32 i;

    if (obs == NULL || key == 0u || !obs_valid_metric_kind(kind) || !obs_valid_visibility(visibility) ||
        !obs_valid_unit(unit) || !obs_valid_scope_kind(scope_kind)) {
        return AI_OBS_INVALID_METRIC;
    }
    if (obs->metric_registry.count >= AI_OBS_MAX_METRICS) {
        return AI_OBS_INVALID_METRIC;
    }
    if (kind == AI_OBS_METRIC_HISTOGRAM) {
        if (!obs_valid_hist_bounds(hist_bounds, hist_bound_count)) {
            return AI_OBS_INVALID_METRIC;
        }
    } else if (hist_bound_count != 0u || hist_bounds != NULL) {
        return AI_OBS_INVALID_METRIC;
    }

    for (i = 0u; i < obs->metric_registry.count; ++i) {
        const ai_obs_metric *existing = &obs->metric_registry.metrics[i];
        if (existing->key == key && existing->scope_kind == scope_kind && existing->scope_id == scope_id) {
            return AI_OBS_INVALID_METRIC;
        }
    }

    m = &obs->metric_registry.metrics[obs->metric_registry.count++];
    obs_zero_metric(m);
    m->id = obs->metric_registry.next_id++;
    m->key = key;
    m->kind = kind;
    m->unit = unit;
    m->scope_kind = scope_kind;
    m->scope_id = scope_id;
    m->visibility = visibility;
    m->last_update_ns = now_ns;

    if (kind == AI_OBS_METRIC_HISTOGRAM) {
        m->hist_bound_count = hist_bound_count;
        for (i = 0u; i < hist_bound_count; ++i) {
            m->hist_bounds[i] = hist_bounds[i];
        }
    }

    ai_obs_trace_append(
        obs,
        AI_OBS_TRACE_METRIC_REGISTER,
        AI_OBS_SEV_DEBUG,
        visibility,
        0ull,
        0ull,
        (u64)m->id,
        scope_id,
        (u64)key,
        (u64)kind,
        now_ns
    );
    return m->id;
}

bool ai_obs_metric_read(
    const ai_observability *obs,
    u32 metric_id,
    ai_obs_visibility clearance,
    ai_obs_metric *out_metric
) {
    const ai_obs_metric *m = ai_obs_metric_find_const(obs, metric_id);
    if (m == NULL || out_metric == NULL || !obs_valid_visibility(clearance)) {
        return false;
    }
    if (m->visibility > clearance) {
        return false;
    }
    *out_metric = *m;
    return true;
}

bool ai_obs_counter_add(ai_observability *obs, u32 metric_id, u64 delta, u64 now_ns) {
    ai_obs_metric *m = obs_metric_find(obs, metric_id);
    if (m == NULL || m->kind != AI_OBS_METRIC_COUNTER) {
        if (obs != NULL) {
            obs->rejected_metric_updates++;
        }
        return false;
    }
    m->value = obs_sat_add(m->value, delta);
    m->updates = obs_sat_add(m->updates, 1ull);
    m->last_update_ns = now_ns;
    return true;
}

bool ai_obs_gauge_set(ai_observability *obs, u32 metric_id, u64 value, u64 now_ns) {
    ai_obs_metric *m = obs_metric_find(obs, metric_id);
    if (m == NULL || m->kind != AI_OBS_METRIC_GAUGE) {
        if (obs != NULL) {
            obs->rejected_metric_updates++;
        }
        return false;
    }
    m->value = value;
    m->updates = obs_sat_add(m->updates, 1ull);
    m->last_update_ns = now_ns;
    return true;
}

bool ai_obs_hist_observe(ai_observability *obs, u32 metric_id, u64 value, u64 now_ns) {
    ai_obs_metric *m = obs_metric_find(obs, metric_id);
    u32 bucket;
    if (m == NULL || m->kind != AI_OBS_METRIC_HISTOGRAM) {
        if (obs != NULL) {
            obs->rejected_metric_updates++;
        }
        return false;
    }

    bucket = m->hist_bound_count;
    {
        u32 i;
        for (i = 0u; i < m->hist_bound_count; ++i) {
            if (value <= m->hist_bounds[i]) {
                bucket = i;
                break;
            }
        }
    }

    m->hist_buckets[bucket] = obs_sat_add(m->hist_buckets[bucket], 1ull);
    m->hist_count = obs_sat_add(m->hist_count, 1ull);
    m->hist_sum = obs_sat_add(m->hist_sum, value);
    if (m->hist_count == 1ull) {
        m->hist_min = value;
        m->hist_max = value;
    } else {
        if (value < m->hist_min) {
            m->hist_min = value;
        }
        if (value > m->hist_max) {
            m->hist_max = value;
        }
    }
    m->value = value;
    m->updates = obs_sat_add(m->updates, 1ull);
    m->last_update_ns = now_ns;
    return true;
}

u32 ai_obs_trace_count(const ai_observability *obs) {
    return obs == NULL ? 0u : obs->trace.count;
}

bool ai_obs_trace_get_oldest(const ai_observability *obs, u32 index, ai_obs_trace_event *out_event) {
    u32 physical;
    if (obs == NULL || out_event == NULL || index >= obs->trace.count) {
        return false;
    }
    physical = (obs->trace.head + index) % AI_OBS_TRACE_CAPACITY;
    *out_event = obs->trace.events[physical];
    return true;
}

static u64 obs_fnv_u64(u64 hash, u64 value) {
    u32 i;
    for (i = 0u; i < 8u; ++i) {
        hash ^= (value >> (i * 8u)) & 0xffull;
        hash *= 1099511628211ull;
    }
    return hash;
}

u64 ai_obs_trace_checksum(const ai_observability *obs) {
    u64 hash = 1469598103934665603ull;
    u32 i;
    ai_obs_trace_event e;
    if (obs == NULL) {
        return 0ull;
    }
    hash = obs_fnv_u64(hash, obs->trace.dropped_events);
    for (i = 0u; i < obs->trace.count; ++i) {
        if (!ai_obs_trace_get_oldest(obs, i, &e)) {
            return 0ull;
        }
        hash = obs_fnv_u64(hash, e.sequence);
        hash = obs_fnv_u64(hash, e.timestamp_ns);
        hash = obs_fnv_u64(hash, (u64)e.kind);
        hash = obs_fnv_u64(hash, (u64)e.severity);
        hash = obs_fnv_u64(hash, (u64)e.visibility);
        hash = obs_fnv_u64(hash, e.span_id);
        hash = obs_fnv_u64(hash, e.correlation_id);
        hash = obs_fnv_u64(hash, e.actor_id);
        hash = obs_fnv_u64(hash, e.object_id);
        hash = obs_fnv_u64(hash, e.arg0);
        hash = obs_fnv_u64(hash, e.arg1);
    }
    return hash;
}

u64 ai_obs_metric_checksum(const ai_observability *obs) {
    u64 hash = 1469598103934665603ull;
    u32 i;
    if (obs == NULL) {
        return 0ull;
    }
    for (i = 0u; i < obs->metric_registry.count; ++i) {
        const ai_obs_metric *m = &obs->metric_registry.metrics[i];
        u32 j;
        hash = obs_fnv_u64(hash, (u64)m->id);
        hash = obs_fnv_u64(hash, (u64)m->key);
        hash = obs_fnv_u64(hash, (u64)m->kind);
        hash = obs_fnv_u64(hash, (u64)m->unit);
        hash = obs_fnv_u64(hash, (u64)m->scope_kind);
        hash = obs_fnv_u64(hash, (u64)m->visibility);
        hash = obs_fnv_u64(hash, m->scope_id);
        hash = obs_fnv_u64(hash, m->value);
        hash = obs_fnv_u64(hash, m->updates);
        hash = obs_fnv_u64(hash, m->last_update_ns);
        hash = obs_fnv_u64(hash, m->hist_count);
        hash = obs_fnv_u64(hash, m->hist_sum);
        hash = obs_fnv_u64(hash, m->hist_min);
        hash = obs_fnv_u64(hash, m->hist_max);
        hash = obs_fnv_u64(hash, (u64)m->hist_bound_count);
        for (j = 0u; j < m->hist_bound_count; ++j) {
            hash = obs_fnv_u64(hash, m->hist_bounds[j]);
        }
        for (j = 0u; j < m->hist_bound_count + 1u; ++j) {
            hash = obs_fnv_u64(hash, m->hist_buckets[j]);
        }
    }
    return hash;
}

u32 ai_obs_trace_export(
    const ai_observability *obs,
    ai_obs_visibility clearance,
    u64 from_sequence,
    ai_obs_trace_event *out_events,
    u32 max_events,
    u64 *next_sequence
) {
    u32 i;
    u32 copied = 0u;
    u64 cursor = from_sequence;

    if (next_sequence != NULL) {
        *next_sequence = from_sequence;
    }
    if (obs == NULL || !obs_valid_visibility(clearance)) {
        return 0u;
    }
    if (max_events > 0u && out_events == NULL) {
        return 0u;
    }

    if (cursor == 0ull && obs->trace.count > 0u) {
        ai_obs_trace_event oldest;
        if (ai_obs_trace_get_oldest(obs, 0u, &oldest)) {
            cursor = oldest.sequence;
        }
    }

    for (i = 0u; i < obs->trace.count; ++i) {
        ai_obs_trace_event e;
        if (!ai_obs_trace_get_oldest(obs, i, &e)) {
            break;
        }
        if (e.sequence < cursor) {
            continue;
        }
        cursor = e.sequence + 1ull;
        if (e.visibility <= clearance && copied < max_events) {
            out_events[copied++] = e;
        }
        if (copied == max_events && max_events > 0u) {
            break;
        }
    }

    if (next_sequence != NULL) {
        *next_sequence = cursor;
    }
    return copied;
}

static ai_obs_span *obs_span_find(ai_observability *obs, u64 span_id) {
    u32 i;
    if (obs == NULL || span_id == AI_OBS_INVALID_SPAN) {
        return NULL;
    }
    for (i = 0u; i < AI_OBS_MAX_ACTIVE_SPANS; ++i) {
        if (obs->span_registry.spans[i].active && obs->span_registry.spans[i].id == span_id) {
            return &obs->span_registry.spans[i];
        }
    }
    return NULL;
}

u64 ai_obs_span_begin(
    ai_observability *obs,
    ai_obs_span_kind kind,
    ai_obs_visibility visibility,
    u64 correlation_id,
    u64 actor_id,
    u64 object_id,
    u64 now_ns
) {
    u32 i;
    ai_obs_span *span = NULL;
    if (obs == NULL) {
        return AI_OBS_INVALID_SPAN;
    }
    if (!obs_valid_visibility(visibility) || !obs_valid_span_kind(kind)) {
        obs->rejected_spans++;
        return AI_OBS_INVALID_SPAN;
    }
    for (i = 0u; i < AI_OBS_MAX_ACTIVE_SPANS; ++i) {
        if (!obs->span_registry.spans[i].active) {
            span = &obs->span_registry.spans[i];
            break;
        }
    }
    if (span == NULL) {
        obs->rejected_spans++;
        return AI_OBS_INVALID_SPAN;
    }

    obs_zero_span(span);
    span->id = obs->span_registry.next_id++;
    span->kind = kind;
    span->visibility = visibility;
    span->correlation_id = correlation_id;
    span->actor_id = actor_id;
    span->object_id = object_id;
    span->start_ns = now_ns;
    span->active = true;
    obs->span_registry.active_count++;
    if (obs->span_registry.active_count > obs->span_registry.peak_active) {
        obs->span_registry.peak_active = obs->span_registry.active_count;
    }

    ai_obs_trace_append(
        obs,
        AI_OBS_TRACE_SPAN_BEGIN,
        AI_OBS_SEV_DEBUG,
        visibility,
        span->id,
        correlation_id,
        actor_id,
        object_id,
        (u64)kind,
        0ull,
        now_ns
    );
    return span->id;
}

bool ai_obs_span_end(
    ai_observability *obs,
    u64 span_id,
    u32 duration_metric_id,
    u64 result_code,
    u64 now_ns
) {
    ai_obs_span *span = obs_span_find(obs, span_id);
    u64 duration;
    if (span == NULL) {
        if (obs != NULL) {
            obs->rejected_spans++;
        }
        return false;
    }

    if (now_ns < span->start_ns) {
        duration = 0ull;
        obs->clock_regressions++;
    } else {
        duration = now_ns - span->start_ns;
    }

    if (duration_metric_id != AI_OBS_INVALID_METRIC) {
        (void)ai_obs_hist_observe(obs, duration_metric_id, duration, now_ns);
    }

    ai_obs_trace_append(
        obs,
        AI_OBS_TRACE_SPAN_END,
        result_code == 0ull ? AI_OBS_SEV_DEBUG : AI_OBS_SEV_WARNING,
        span->visibility,
        span->id,
        span->correlation_id,
        span->actor_id,
        span->object_id,
        duration,
        result_code,
        now_ns
    );
    span->active = false;
    if (obs->span_registry.active_count > 0u) {
        obs->span_registry.active_count--;
    }
    return true;
}

bool ai_obs_span_cancel(ai_observability *obs, u64 span_id, u64 reason_code, u64 now_ns) {
    ai_obs_span *span = obs_span_find(obs, span_id);
    if (span == NULL) {
        if (obs != NULL) {
            obs->rejected_spans++;
        }
        return false;
    }
    ai_obs_trace_append(
        obs,
        AI_OBS_TRACE_SPAN_CANCEL,
        AI_OBS_SEV_WARNING,
        span->visibility,
        span->id,
        span->correlation_id,
        span->actor_id,
        span->object_id,
        (u64)span->kind,
        reason_code,
        now_ns
    );
    span->active = false;
    if (obs->span_registry.active_count > 0u) {
        obs->span_registry.active_count--;
    }
    return true;
}

ai_obs_snapshot ai_obs_snapshot_capture(ai_observability *obs, u64 now_ns) {
    ai_obs_snapshot s;
    s.snapshot_id = 0ull;
    s.timestamp_ns = now_ns;
    s.metric_count = 0u;
    s.active_spans = 0u;
    s.trace_next_sequence = 0ull;
    s.trace_count = 0u;
    s.trace_dropped_events = 0ull;
    s.trace_checksum = 0ull;
    s.metric_checksum = 0ull;
    s.rejected_metric_updates = 0ull;
    s.rejected_spans = 0ull;
    s.clock_regressions = 0ull;

    if (obs == NULL) {
        return s;
    }

    s.snapshot_id = obs->next_snapshot_id++;
    s.metric_count = obs->metric_registry.count;
    s.active_spans = obs->span_registry.active_count;
    s.trace_next_sequence = obs->trace.next_sequence;
    s.trace_count = obs->trace.count;
    s.trace_dropped_events = obs->trace.dropped_events;
    s.trace_checksum = ai_obs_trace_checksum(obs);
    s.metric_checksum = ai_obs_metric_checksum(obs);
    s.rejected_metric_updates = obs->rejected_metric_updates;
    s.rejected_spans = obs->rejected_spans;
    s.clock_regressions = obs->clock_regressions;

    ai_obs_trace_append(
        obs,
        AI_OBS_TRACE_DIAGNOSTIC_SNAPSHOT,
        AI_OBS_SEV_INFO,
        AI_OBS_VIS_SENSITIVE,
        0ull,
        0ull,
        s.snapshot_id,
        0ull,
        s.trace_checksum,
        s.metric_checksum,
        now_ns
    );
    return s;
}

const char *ai_obs_metric_kind_name(ai_obs_metric_kind kind) {
    switch (kind) {
        case AI_OBS_METRIC_COUNTER: return "counter";
        case AI_OBS_METRIC_GAUGE: return "gauge";
        case AI_OBS_METRIC_HISTOGRAM: return "histogram";
        default: return "unknown";
    }
}

const char *ai_obs_trace_name(ai_obs_trace_kind kind) {
    switch (kind) {
        case AI_OBS_TRACE_BOOT: return "boot";
        case AI_OBS_TRACE_METRIC_REGISTER: return "metric-register";
        case AI_OBS_TRACE_SPAN_BEGIN: return "span-begin";
        case AI_OBS_TRACE_SPAN_END: return "span-end";
        case AI_OBS_TRACE_SPAN_CANCEL: return "span-cancel";
        case AI_OBS_TRACE_WORK_SUBMIT: return "work-submit";
        case AI_OBS_TRACE_SCHED_PICK: return "sched-pick";
        case AI_OBS_TRACE_RESOURCE_ASSIGN: return "resource-assign";
        case AI_OBS_TRACE_TRANSFER_BEGIN: return "transfer-begin";
        case AI_OBS_TRACE_TRANSFER_END: return "transfer-end";
        case AI_OBS_TRACE_EXEC_BEGIN: return "exec-begin";
        case AI_OBS_TRACE_EXEC_END: return "exec-end";
        case AI_OBS_TRACE_FAULT: return "fault";
        case AI_OBS_TRACE_RECOVERY: return "recovery";
        case AI_OBS_TRACE_CHECKPOINT: return "checkpoint";
        case AI_OBS_TRACE_SAFE_MODE: return "safe-mode";
        case AI_OBS_TRACE_RELIABILITY: return "reliability";
        case AI_OBS_TRACE_SOURCE_GAP: return "source-gap";
        case AI_OBS_TRACE_DIAGNOSTIC_SNAPSHOT: return "diagnostic-snapshot";
        case AI_OBS_TRACE_CUSTOM: return "custom";
        default: return "unknown";
    }
}

const char *ai_obs_span_kind_name(ai_obs_span_kind kind) {
    switch (kind) {
        case AI_OBS_SPAN_WORK: return "work";
        case AI_OBS_SPAN_SCHEDULER: return "scheduler";
        case AI_OBS_SPAN_DEVICE: return "device";
        case AI_OBS_SPAN_DMA: return "dma";
        case AI_OBS_SPAN_NETWORK: return "network";
        case AI_OBS_SPAN_RECOVERY: return "recovery";
        case AI_OBS_SPAN_CHECKPOINT: return "checkpoint";
        case AI_OBS_SPAN_SYSCALL: return "syscall";
        case AI_OBS_SPAN_AGENT: return "agent";
        default: return "unknown";
    }
}
