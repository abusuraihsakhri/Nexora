#include <ai/telemetry.h>

void ai_telemetry_init(ai_telemetry_buffer *buffer) {
    if (!buffer) return;
    buffer->write_index = 0;
    buffer->count = 0;
    buffer->next_seq = 1;
    buffer->dropped = 0;
}

void ai_telemetry_emit(
    ai_telemetry_buffer *buffer,
    u64 timestamp_ns,
    ai_telemetry_kind kind,
    u32 cpu_id,
    u64 object_id,
    u64 value0,
    u64 value1
) {
    if (!buffer) return;

    ai_telemetry_event *event = &buffer->events[buffer->write_index];
    event->seq = buffer->next_seq++;
    event->timestamp_ns = timestamp_ns;
    event->kind = (u32)kind;
    event->cpu_id = cpu_id;
    event->object_id = object_id;
    event->value0 = value0;
    event->value1 = value1;

    buffer->write_index = (buffer->write_index + 1U) % AI_TELEMETRY_CAPACITY;
    if (buffer->count < AI_TELEMETRY_CAPACITY) {
        buffer->count++;
    } else {
        buffer->dropped++;
    }
}

u32 ai_telemetry_count(const ai_telemetry_buffer *buffer) {
    return buffer ? buffer->count : 0;
}

u64 ai_telemetry_dropped(const ai_telemetry_buffer *buffer) {
    return buffer ? buffer->dropped : 0;
}

bool ai_telemetry_get_oldest(
    const ai_telemetry_buffer *buffer,
    u32 ordinal,
    ai_telemetry_event *out
) {
    if (!buffer || !out || ordinal >= buffer->count) return false;

    u32 oldest = 0;
    if (buffer->count == AI_TELEMETRY_CAPACITY) {
        oldest = buffer->write_index;
    }
    u32 index = (oldest + ordinal) % AI_TELEMETRY_CAPACITY;
    *out = buffer->events[index];
    return true;
}
