#ifndef NEXORA_AI_TELEMETRY_H
#define NEXORA_AI_TELEMETRY_H

#include <kernel/types.h>

/* Single-writer buffer: use one instance per CPU or protect externally. */
#define AI_TELEMETRY_CAPACITY 2048U

typedef enum {
    AI_TELEM_BOOT = 1,
    AI_TELEM_ALLOC,
    AI_TELEM_FREE,
    AI_TELEM_WORK_SUBMIT,
    AI_TELEM_WORK_DISPATCH,
    AI_TELEM_WORK_COMPLETE,
    AI_TELEM_WORK_FAIL,
    AI_TELEM_TRANSFER,
    AI_TELEM_CAP_CHECK,
    AI_TELEM_REMOTE_OP,
    AI_TELEM_FAULT_INJECT,
    AI_TELEM_HEALTH
} ai_telemetry_kind;

typedef struct {
    u64 seq;
    u64 timestamp_ns;
    u32 kind;
    u32 cpu_id;
    u64 object_id;
    u64 value0;
    u64 value1;
} ai_telemetry_event;

typedef struct {
    ai_telemetry_event events[AI_TELEMETRY_CAPACITY];
    u32 write_index;
    u32 count;
    u64 next_seq;
    u64 dropped;
} ai_telemetry_buffer;

void ai_telemetry_init(ai_telemetry_buffer *buffer);
void ai_telemetry_emit(
    ai_telemetry_buffer *buffer,
    u64 timestamp_ns,
    ai_telemetry_kind kind,
    u32 cpu_id,
    u64 object_id,
    u64 value0,
    u64 value1
);

u32 ai_telemetry_count(const ai_telemetry_buffer *buffer);
u64 ai_telemetry_dropped(const ai_telemetry_buffer *buffer);
bool ai_telemetry_get_oldest(
    const ai_telemetry_buffer *buffer,
    u32 ordinal,
    ai_telemetry_event *out
);

#endif
