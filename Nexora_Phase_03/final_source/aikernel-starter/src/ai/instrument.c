#include <ai/instrument.h>
#include <kernel/memory_object.h>

static ai_trace_event trace_events[AI_TRACE_CAPACITY];
static u32 trace_count_value;
static u64 next_sequence;
static bool trace_enabled_value;
static ai_instrument_stats stats;

void ai_instrument_system_init(void) {
    trace_count_value = 0;
    next_sequence = 1;
    trace_enabled_value = true;
    stats.events_recorded = 0;
    stats.events_dropped = 0;
    stats.sampled_peak_resident_bytes = 0;
    stats.memory_samples = 0;
}

void ai_trace_reset(void) {
    trace_count_value = 0;
    next_sequence = 1;
    stats.events_recorded = 0;
    stats.events_dropped = 0;
    stats.sampled_peak_resident_bytes = 0;
    stats.memory_samples = 0;
}

void ai_trace_set_enabled(bool enabled) {
    trace_enabled_value = enabled;
}

bool ai_trace_enabled(void) {
    return trace_enabled_value;
}

void ai_trace_record(
    ai_trace_type type,
    u64 object_id,
    u64 related_id,
    u64 value
) {
    if (!trace_enabled_value || type >= AI_TRACE_TYPE_COUNT) {
        return;
    }

    const u64 resident = nx_memory_get_stats()->resident_bytes;
    if (resident > stats.sampled_peak_resident_bytes) {
        stats.sampled_peak_resident_bytes = resident;
    }

    if (trace_count_value >= AI_TRACE_CAPACITY) {
        ++stats.events_dropped;
        return;
    }

    ai_trace_event *event = &trace_events[trace_count_value++];
    event->sequence = next_sequence++;
    event->type = type;
    event->object_id = object_id;
    event->related_id = related_id;
    event->value = value;
    event->resident_bytes = resident;
    ++stats.events_recorded;
}

void ai_trace_memory_sample(u64 tag) {
    ++stats.memory_samples;
    ai_trace_record(AI_TRACE_MEMORY_SAMPLE, 0, 0, tag);
}

u32 ai_trace_count(void) {
    return trace_count_value;
}

const ai_trace_event *ai_trace_at(u32 index) {
    return index < trace_count_value ? &trace_events[index] : NULL;
}

const ai_instrument_stats *ai_instrument_get_stats(void) {
    return &stats;
}

const char *ai_trace_type_name(ai_trace_type type) {
    switch (type) {
        case AI_TRACE_TENSOR_CREATE: return "tensor-create";
        case AI_TRACE_TENSOR_BIND: return "tensor-bind";
        case AI_TRACE_TENSOR_UNBIND: return "tensor-unbind";
        case AI_TRACE_TENSOR_DESTROY: return "tensor-destroy";
        case AI_TRACE_WORK_CREATE: return "work-create";
        case AI_TRACE_WORK_DISPATCH: return "work-dispatch";
        case AI_TRACE_WORK_COMPLETE: return "work-complete";
        case AI_TRACE_TENSOR_RECLAIM: return "tensor-reclaim";
        case AI_TRACE_RECLAIM_DEFER: return "reclaim-defer";
        case AI_TRACE_MEMORY_SAMPLE: return "memory-sample";
        default: return "unknown";
    }
}
