#ifndef NEXORA_AI_LIFETIME_H
#define NEXORA_AI_LIFETIME_H

#include <kernel/types.h>

struct ai_tensor;

typedef enum {
    /* Infer from legacy tensor flags for compatibility. */
    AI_TENSOR_LIFETIME_AUTO = 0,
    AI_TENSOR_LIFETIME_TEMPORARY,
    AI_TENSOR_LIFETIME_PERSISTENT,
    AI_TENSOR_LIFETIME_CACHED,
    AI_TENSOR_LIFETIME_SHARED,
    AI_TENSOR_LIFETIME_EXTERNAL,
    AI_TENSOR_LIFETIME_COUNT
} ai_tensor_lifetime;

typedef enum {
    AI_TENSOR_RESIDENCY_UNBACKED = 0,
    AI_TENSOR_RESIDENCY_RESIDENT,
    AI_TENSOR_RESIDENCY_RECLAIMED
} ai_tensor_residency;

typedef enum {
    AI_RECLAIM_FINAL_CONSUMER = 0,
    AI_RECLAIM_MEMORY_PRESSURE,
    AI_RECLAIM_CACHE_EVICTION,
    AI_RECLAIM_EXPLICIT,
    AI_RECLAIM_REASON_COUNT
} ai_reclaim_reason;

typedef enum {
    AI_LIFETIME_OK = 0,
    AI_LIFETIME_ERR_INVALID_ARGUMENT,
    AI_LIFETIME_ERR_NOT_BACKED,
    AI_LIFETIME_ERR_POLICY_DENIED,
    AI_LIFETIME_ERR_PINNED,
    AI_LIFETIME_ERR_SHARED_BUSY,
    AI_LIFETIME_ERR_UNBIND_FAILED
} ai_lifetime_status;

typedef struct {
    u64 live_by_class[AI_TENSOR_LIFETIME_COUNT];
    u64 reclaim_attempts;
    u64 reclaim_successes;
    u64 reclaim_denied;
    u64 binding_bytes_released;
    u64 resident_bytes_reclaimed;
} ai_lifetime_stats;

void ai_lifetime_system_init(void);

/* Internal lifecycle hooks used by the tensor object implementation. */
void ai_lifetime_track_create(struct ai_tensor *tensor);
void ai_lifetime_track_destroy(struct ai_tensor *tensor);

bool ai_tensor_lifetime_can_reclaim(
    const struct ai_tensor *tensor,
    ai_reclaim_reason reason
);

ai_lifetime_status ai_tensor_lifetime_reclaim(
    struct ai_tensor *tensor,
    ai_reclaim_reason reason
);

const ai_lifetime_stats *ai_lifetime_get_stats(void);
const char *ai_tensor_lifetime_name(ai_tensor_lifetime lifetime);
const char *ai_tensor_residency_name(ai_tensor_residency residency);
const char *ai_reclaim_reason_name(ai_reclaim_reason reason);
const char *ai_lifetime_status_name(ai_lifetime_status status);

#endif
