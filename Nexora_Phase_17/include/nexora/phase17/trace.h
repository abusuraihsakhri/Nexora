#ifndef NEXORA_PHASE17_TRACE_H
#define NEXORA_PHASE17_TRACE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NX_TRACE_CAPACITY 1024u

typedef enum nx_trace_severity {
    NX_TRACE_DEBUG = 0,
    NX_TRACE_INFO = 1,
    NX_TRACE_WARN = 2,
    NX_TRACE_ERROR = 3,
    NX_TRACE_FATAL = 4
} nx_trace_severity_t;

typedef struct nx_trace_event {
    uint64_t sequence;
    uint64_t timestamp_ns;
    uint32_t component_id;
    uint16_t event_id;
    uint8_t severity;
    uint8_t flags;
    uint64_t arg0;
    uint64_t arg1;
} nx_trace_event_t;

void nx_trace_reset(void);
uint64_t nx_trace_emit(uint64_t timestamp_ns,
                       uint32_t component_id,
                       uint16_t event_id,
                       nx_trace_severity_t severity,
                       uint8_t flags,
                       uint64_t arg0,
                       uint64_t arg1);
uint64_t nx_trace_count(void);
size_t nx_trace_copy_latest(nx_trace_event_t *out, size_t capacity);
uint64_t nx_trace_checksum_latest(size_t max_events);

#ifdef __cplusplus
}
#endif

#endif
