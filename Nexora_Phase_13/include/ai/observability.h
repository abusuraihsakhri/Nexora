#ifndef NEXORA_AI_OBSERVABILITY_H
#define NEXORA_AI_OBSERVABILITY_H

#include <kernel/types.h>

#define AI_OBS_MAX_METRICS        64u
#define AI_OBS_MAX_ACTIVE_SPANS   64u
#define AI_OBS_TRACE_CAPACITY     512u
#define AI_OBS_MAX_HIST_BOUNDS    16u
#define AI_OBS_INVALID_METRIC     0u
#define AI_OBS_INVALID_SPAN       0ull

/*
 * Phase 13 observability core.
 *
 * Design constraints:
 * - fixed memory only
 * - no libc and no heap allocation
 * - caller-supplied timestamps
 * - numeric metric keys instead of kernel-owned strings
 * - bounded traces and spans with explicit drop/reject accounting
 * - visibility labels are enforced by export helpers
 * - observability records facts; it does not authorize, schedule, or recover work
 */

typedef enum {
    AI_OBS_METRIC_COUNTER = 1,
    AI_OBS_METRIC_GAUGE,
    AI_OBS_METRIC_HISTOGRAM
} ai_obs_metric_kind;

typedef enum {
    AI_OBS_UNIT_NONE = 0,
    AI_OBS_UNIT_COUNT,
    AI_OBS_UNIT_BYTES,
    AI_OBS_UNIT_NANOSECONDS,
    AI_OBS_UNIT_ITEMS,
    AI_OBS_UNIT_PERCENT_MILLI
} ai_obs_unit;

typedef enum {
    AI_OBS_SCOPE_SYSTEM = 0,
    AI_OBS_SCOPE_SCHEDULER,
    AI_OBS_SCOPE_DEVICE,
    AI_OBS_SCOPE_AGENT,
    AI_OBS_SCOPE_REMOTE_NODE,
    AI_OBS_SCOPE_NETWORK_PATH,
    AI_OBS_SCOPE_WORK_GRAPH,
    AI_OBS_SCOPE_TENSOR,
    AI_OBS_SCOPE_SERVICE
} ai_obs_scope_kind;

typedef enum {
    AI_OBS_VIS_PUBLIC = 0,
    AI_OBS_VIS_OPERATOR,
    AI_OBS_VIS_SENSITIVE
} ai_obs_visibility;

typedef enum {
    AI_OBS_SEV_DEBUG = 0,
    AI_OBS_SEV_INFO,
    AI_OBS_SEV_WARNING,
    AI_OBS_SEV_ERROR,
    AI_OBS_SEV_FATAL
} ai_obs_severity;

typedef enum {
    AI_OBS_TRACE_BOOT = 1,
    AI_OBS_TRACE_METRIC_REGISTER,
    AI_OBS_TRACE_SPAN_BEGIN,
    AI_OBS_TRACE_SPAN_END,
    AI_OBS_TRACE_SPAN_CANCEL,
    AI_OBS_TRACE_WORK_SUBMIT,
    AI_OBS_TRACE_SCHED_PICK,
    AI_OBS_TRACE_RESOURCE_ASSIGN,
    AI_OBS_TRACE_TRANSFER_BEGIN,
    AI_OBS_TRACE_TRANSFER_END,
    AI_OBS_TRACE_EXEC_BEGIN,
    AI_OBS_TRACE_EXEC_END,
    AI_OBS_TRACE_FAULT,
    AI_OBS_TRACE_RECOVERY,
    AI_OBS_TRACE_CHECKPOINT,
    AI_OBS_TRACE_SAFE_MODE,
    AI_OBS_TRACE_RELIABILITY,
    AI_OBS_TRACE_SOURCE_GAP,
    AI_OBS_TRACE_DIAGNOSTIC_SNAPSHOT,
    AI_OBS_TRACE_CUSTOM
} ai_obs_trace_kind;

typedef enum {
    AI_OBS_SPAN_WORK = 1,
    AI_OBS_SPAN_SCHEDULER,
    AI_OBS_SPAN_DEVICE,
    AI_OBS_SPAN_DMA,
    AI_OBS_SPAN_NETWORK,
    AI_OBS_SPAN_RECOVERY,
    AI_OBS_SPAN_CHECKPOINT,
    AI_OBS_SPAN_SYSCALL,
    AI_OBS_SPAN_AGENT
} ai_obs_span_kind;

typedef struct {
    u32 id;
    u32 key;
    ai_obs_metric_kind kind;
    ai_obs_unit unit;
    ai_obs_scope_kind scope_kind;
    ai_obs_visibility visibility;
    u64 scope_id;
    u64 value;
    u64 updates;
    u64 last_update_ns;
    u64 hist_count;
    u64 hist_sum;
    u64 hist_min;
    u64 hist_max;
    u32 hist_bound_count;
    u64 hist_bounds[AI_OBS_MAX_HIST_BOUNDS];
    u64 hist_buckets[AI_OBS_MAX_HIST_BOUNDS + 1u];
} ai_obs_metric;

typedef struct {
    ai_obs_metric metrics[AI_OBS_MAX_METRICS];
    u32 count;
    u32 next_id;
} ai_obs_metric_registry;

typedef struct {
    u64 sequence;
    u64 timestamp_ns;
    ai_obs_trace_kind kind;
    ai_obs_severity severity;
    ai_obs_visibility visibility;
    u64 span_id;
    u64 correlation_id;
    u64 actor_id;
    u64 object_id;
    u64 arg0;
    u64 arg1;
} ai_obs_trace_event;

typedef struct {
    ai_obs_trace_event events[AI_OBS_TRACE_CAPACITY];
    u32 head;
    u32 count;
    u64 next_sequence;
    u64 dropped_events;
} ai_obs_trace_ring;

typedef struct {
    u64 id;
    ai_obs_span_kind kind;
    ai_obs_visibility visibility;
    u64 correlation_id;
    u64 actor_id;
    u64 object_id;
    u64 start_ns;
    bool active;
} ai_obs_span;

typedef struct {
    ai_obs_span spans[AI_OBS_MAX_ACTIVE_SPANS];
    u32 active_count;
    u32 peak_active;
    u64 next_id;
} ai_obs_span_registry;

typedef struct {
    u64 snapshot_id;
    u64 timestamp_ns;
    u32 metric_count;
    u32 active_spans;
    u64 trace_next_sequence;
    u32 trace_count;
    u64 trace_dropped_events;
    u64 trace_checksum;
    u64 metric_checksum;
    u64 rejected_metric_updates;
    u64 rejected_spans;
    u64 clock_regressions;
} ai_obs_snapshot;

typedef struct {
    ai_obs_metric_registry metric_registry;
    ai_obs_trace_ring trace;
    ai_obs_span_registry span_registry;
    u64 next_snapshot_id;
    u64 rejected_metric_updates;
    u64 rejected_spans;
    u64 clock_regressions;
} ai_observability;

/* Initialization. */
void ai_obs_init(ai_observability *obs, u64 now_ns);

/* Metrics. Metric keys are stable numeric identifiers owned by the integration layer. */
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
);
const ai_obs_metric *ai_obs_metric_find_const(const ai_observability *obs, u32 metric_id);
bool ai_obs_metric_read(
    const ai_observability *obs,
    u32 metric_id,
    ai_obs_visibility clearance,
    ai_obs_metric *out_metric
);
bool ai_obs_counter_add(ai_observability *obs, u32 metric_id, u64 delta, u64 now_ns);
bool ai_obs_gauge_set(ai_observability *obs, u32 metric_id, u64 value, u64 now_ns);
bool ai_obs_hist_observe(ai_observability *obs, u32 metric_id, u64 value, u64 now_ns);
u64 ai_obs_metric_checksum(const ai_observability *obs);

/* Tracing. Raw access is for trusted in-kernel callers; export enforces visibility. */
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
);
u32 ai_obs_trace_count(const ai_observability *obs);
bool ai_obs_trace_get_oldest(const ai_observability *obs, u32 index, ai_obs_trace_event *out_event);
u64 ai_obs_trace_checksum(const ai_observability *obs);
u32 ai_obs_trace_export(
    const ai_observability *obs,
    ai_obs_visibility clearance,
    u64 from_sequence,
    ai_obs_trace_event *out_events,
    u32 max_events,
    u64 *next_sequence
);

/* Bounded latency spans. duration_metric_id may be 0 to disable histogram recording. */
u64 ai_obs_span_begin(
    ai_observability *obs,
    ai_obs_span_kind kind,
    ai_obs_visibility visibility,
    u64 correlation_id,
    u64 actor_id,
    u64 object_id,
    u64 now_ns
);
bool ai_obs_span_end(
    ai_observability *obs,
    u64 span_id,
    u32 duration_metric_id,
    u64 result_code,
    u64 now_ns
);
bool ai_obs_span_cancel(ai_observability *obs, u64 span_id, u64 reason_code, u64 now_ns);

/* Diagnostic snapshot: metadata only; no payload, tensor, or model data is copied. */
ai_obs_snapshot ai_obs_snapshot_capture(ai_observability *obs, u64 now_ns);

/* Readable enum names for trusted console/debug tooling. */
const char *ai_obs_metric_kind_name(ai_obs_metric_kind kind);
const char *ai_obs_trace_name(ai_obs_trace_kind kind);
const char *ai_obs_span_kind_name(ai_obs_span_kind kind);

#endif
