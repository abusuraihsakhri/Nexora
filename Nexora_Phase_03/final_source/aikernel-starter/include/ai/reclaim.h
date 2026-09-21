#ifndef NEXORA_AI_RECLAIM_H
#define NEXORA_AI_RECLAIM_H

#include <kernel/types.h>
#include <ai/lifetime.h>

struct ai_tensor;

#define AI_RECLAIM_DEFERRED_CAPACITY 128u

typedef enum {
    AI_RECLAIM_OK = 0,
    AI_RECLAIM_ERR_INVALID_ARGUMENT,
    AI_RECLAIM_ERR_NO_CANDIDATE,
    AI_RECLAIM_ERR_TARGET_NOT_MET
} ai_reclaim_status;

typedef struct {
    u64 target_resident_bytes;
    u64 resident_before;
    u64 resident_after;
    u64 binding_bytes_released;
    u64 resident_bytes_reclaimed;
    u32 candidates_scanned;
    u32 attempts;
    u32 successes;
    u32 denied;
    u32 skipped_live;
    u32 skipped_pinned;
    bool target_met;
} ai_reclaim_report;

typedef struct {
    u64 final_consumer_events;
    u64 final_consumer_attempts;
    u64 final_consumer_successes;
    u64 final_consumer_deferred;

    u64 deferred_enqueued;
    u64 deferred_duplicate_suppressed;
    u64 deferred_retry_passes;
    u64 deferred_retry_attempts;
    u64 deferred_retry_successes;
    u64 deferred_stale_drops;

    u64 pressure_runs;
    u64 pressure_targets_bytes;
    u64 pressure_binding_bytes_released;
    u64 pressure_resident_bytes_reclaimed;
    u64 pressure_target_met;

    u64 cache_eviction_runs;
    u64 cache_resident_bytes_reclaimed;
} ai_reclaim_stats;

void ai_reclaim_system_init(void);

/* Instrumentation/benchmark control. Enabled by default. */
void ai_reclaim_set_final_consumer_enabled(bool enabled);
bool ai_reclaim_final_consumer_enabled(void);

/*
 * Called by the work graph when a temporary tensor reaches its final consumer.
 * Retryable failures (currently pins/transient unbind failures) are queued.
 */
ai_lifetime_status ai_reclaim_on_final_consumer(
    struct ai_tensor *tensor,
    bool *out_deferred
);

/* Retry non-owning generation-checked tensor handles in the deferred queue. */
u32 ai_reclaim_retry_deferred(void);
u32 ai_reclaim_deferred_count(void);

/*
 * Reclaim graph-dead temporary tensors first, then evict safe cached tensors,
 * until target_resident_bytes have actually left live resident accounting.
 */
ai_reclaim_status ai_reclaim_under_pressure(
    u64 target_resident_bytes,
    ai_reclaim_report *out_report
);

/* Evict only safe cached tensors. */
ai_reclaim_status ai_reclaim_evict_cache(
    u64 target_resident_bytes,
    ai_reclaim_report *out_report
);

const ai_reclaim_stats *ai_reclaim_get_stats(void);
const char *ai_reclaim_status_name(ai_reclaim_status status);

#endif
