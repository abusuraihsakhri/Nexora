#ifndef NEXORA_AI_INSTRUMENT_H
#define NEXORA_AI_INSTRUMENT_H

#include <kernel/types.h>

#define AI_TRACE_CAPACITY 512u

typedef enum {
    AI_TRACE_TENSOR_CREATE = 0,
    AI_TRACE_TENSOR_BIND,
    AI_TRACE_TENSOR_UNBIND,
    AI_TRACE_TENSOR_DESTROY,
    AI_TRACE_WORK_CREATE,
    AI_TRACE_WORK_DISPATCH,
    AI_TRACE_WORK_COMPLETE,
    AI_TRACE_TENSOR_RECLAIM,
    AI_TRACE_RECLAIM_DEFER,
    AI_TRACE_MEMORY_SAMPLE,
    AI_TRACE_TYPE_COUNT
} ai_trace_type;

typedef struct {
    u64 sequence;
    ai_trace_type type;
    u64 object_id;
    u64 related_id;
    u64 value;
    u64 resident_bytes;
} ai_trace_event;

typedef struct {
    u64 events_recorded;
    u64 events_dropped;
    u64 sampled_peak_resident_bytes;
    u64 memory_samples;
} ai_instrument_stats;

void ai_instrument_system_init(void);
void ai_trace_reset(void);
void ai_trace_set_enabled(bool enabled);
bool ai_trace_enabled(void);

void ai_trace_record(
    ai_trace_type type,
    u64 object_id,
    u64 related_id,
    u64 value
);

void ai_trace_memory_sample(u64 tag);

u32 ai_trace_count(void);
const ai_trace_event *ai_trace_at(u32 index);
const ai_instrument_stats *ai_instrument_get_stats(void);
const char *ai_trace_type_name(ai_trace_type type);

#endif
