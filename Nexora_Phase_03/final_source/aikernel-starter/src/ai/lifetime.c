#include <ai/lifetime.h>
#include <ai/tensor.h>
#include <ai/instrument.h>
#include <kernel/memory_object.h>
#include <kernel/object.h>

static ai_lifetime_stats stats;

void ai_lifetime_system_init(void) {
    for (u32 i = 0; i < AI_TENSOR_LIFETIME_COUNT; ++i) {
        stats.live_by_class[i] = 0;
    }
    stats.reclaim_attempts = 0;
    stats.reclaim_successes = 0;
    stats.reclaim_denied = 0;
    stats.binding_bytes_released = 0;
    stats.resident_bytes_reclaimed = 0;
}

void ai_lifetime_track_create(struct ai_tensor *tensor) {
    if (tensor == NULL || tensor->lifetime <= AI_TENSOR_LIFETIME_AUTO ||
        tensor->lifetime >= AI_TENSOR_LIFETIME_COUNT) {
        return;
    }
    ++stats.live_by_class[tensor->lifetime];
}

void ai_lifetime_track_destroy(struct ai_tensor *tensor) {
    if (tensor == NULL || tensor->lifetime <= AI_TENSOR_LIFETIME_AUTO ||
        tensor->lifetime >= AI_TENSOR_LIFETIME_COUNT) {
        return;
    }
    if (stats.live_by_class[tensor->lifetime] > 0) {
        --stats.live_by_class[tensor->lifetime];
    }
}

static bool reason_allowed(ai_tensor_lifetime lifetime, ai_reclaim_reason reason) {
    switch (lifetime) {
        case AI_TENSOR_LIFETIME_TEMPORARY:
            return reason == AI_RECLAIM_FINAL_CONSUMER ||
                   reason == AI_RECLAIM_MEMORY_PRESSURE ||
                   reason == AI_RECLAIM_EXPLICIT;
        case AI_TENSOR_LIFETIME_PERSISTENT:
            return reason == AI_RECLAIM_EXPLICIT;
        case AI_TENSOR_LIFETIME_CACHED:
            return reason == AI_RECLAIM_MEMORY_PRESSURE ||
                   reason == AI_RECLAIM_CACHE_EVICTION ||
                   reason == AI_RECLAIM_EXPLICIT;
        case AI_TENSOR_LIFETIME_SHARED:
        case AI_TENSOR_LIFETIME_EXTERNAL:
            return reason == AI_RECLAIM_EXPLICIT;
        default:
            return false;
    }
}

bool ai_tensor_lifetime_can_reclaim(
    const struct ai_tensor *tensor,
    ai_reclaim_reason reason
) {
    if (tensor == NULL || reason >= AI_RECLAIM_REASON_COUNT ||
        tensor->backing_memory == NX_INVALID_HANDLE ||
        tensor->object.state != NX_OBJECT_LIVE) {
        return false;
    }

    if (!reason_allowed(tensor->lifetime, reason)) {
        return false;
    }

    if ((tensor->flags & AI_TENSOR_PINNED) || tensor->object.pin_count != 0) {
        return false;
    }

    nx_memory *memory = ai_tensor_backing(tensor);
    if (memory == NULL || memory->object.pin_count != 0) {
        return false;
    }

    if (tensor->lifetime == AI_TENSOR_LIFETIME_SHARED &&
        tensor->object.strong_refs > 1) {
        return false;
    }

    return true;
}

ai_lifetime_status ai_tensor_lifetime_reclaim(
    struct ai_tensor *tensor,
    ai_reclaim_reason reason
) {
    if (tensor == NULL || reason >= AI_RECLAIM_REASON_COUNT) {
        return AI_LIFETIME_ERR_INVALID_ARGUMENT;
    }

    ++stats.reclaim_attempts;

    if (tensor->backing_memory == NX_INVALID_HANDLE) {
        ++stats.reclaim_denied;
        return AI_LIFETIME_ERR_NOT_BACKED;
    }

    if (!reason_allowed(tensor->lifetime, reason)) {
        ++stats.reclaim_denied;
        return AI_LIFETIME_ERR_POLICY_DENIED;
    }

    nx_memory *memory = ai_tensor_backing(tensor);
    if (memory == NULL) {
        ++stats.reclaim_denied;
        return AI_LIFETIME_ERR_NOT_BACKED;
    }

    if ((tensor->flags & AI_TENSOR_PINNED) || tensor->object.pin_count != 0 ||
        memory->object.pin_count != 0) {
        ++stats.reclaim_denied;
        return AI_LIFETIME_ERR_PINNED;
    }

    if (tensor->lifetime == AI_TENSOR_LIFETIME_SHARED &&
        tensor->object.strong_refs > 1) {
        ++stats.reclaim_denied;
        return AI_LIFETIME_ERR_SHARED_BUSY;
    }

    const u64 binding_bytes = tensor->storage_span_bytes;
    const u64 resident_before = nx_memory_get_stats()->resident_bytes;

    if (!ai_tensor_unbind_memory(tensor)) {
        ++stats.reclaim_denied;
        return AI_LIFETIME_ERR_UNBIND_FAILED;
    }

    const u64 resident_after = nx_memory_get_stats()->resident_bytes;
    tensor->residency = AI_TENSOR_RESIDENCY_RECLAIMED;
    tensor->last_reclaim_reason = reason;
    ++tensor->reclaim_count;

    ++stats.reclaim_successes;
    stats.binding_bytes_released += binding_bytes;
    if (resident_before >= resident_after) {
        stats.resident_bytes_reclaimed += resident_before - resident_after;
    }
    ai_trace_record(AI_TRACE_TENSOR_RECLAIM, tensor->object.id, (u64)reason, binding_bytes);

    return AI_LIFETIME_OK;
}

const ai_lifetime_stats *ai_lifetime_get_stats(void) {
    return &stats;
}

const char *ai_tensor_lifetime_name(ai_tensor_lifetime lifetime) {
    switch (lifetime) {
        case AI_TENSOR_LIFETIME_AUTO: return "auto";
        case AI_TENSOR_LIFETIME_TEMPORARY: return "temporary";
        case AI_TENSOR_LIFETIME_PERSISTENT: return "persistent";
        case AI_TENSOR_LIFETIME_CACHED: return "cached";
        case AI_TENSOR_LIFETIME_SHARED: return "shared";
        case AI_TENSOR_LIFETIME_EXTERNAL: return "external";
        default: return "unknown";
    }
}

const char *ai_tensor_residency_name(ai_tensor_residency residency) {
    switch (residency) {
        case AI_TENSOR_RESIDENCY_UNBACKED: return "unbacked";
        case AI_TENSOR_RESIDENCY_RESIDENT: return "resident";
        case AI_TENSOR_RESIDENCY_RECLAIMED: return "reclaimed";
        default: return "unknown";
    }
}

const char *ai_reclaim_reason_name(ai_reclaim_reason reason) {
    switch (reason) {
        case AI_RECLAIM_FINAL_CONSUMER: return "final-consumer";
        case AI_RECLAIM_MEMORY_PRESSURE: return "memory-pressure";
        case AI_RECLAIM_CACHE_EVICTION: return "cache-eviction";
        case AI_RECLAIM_EXPLICIT: return "explicit";
        default: return "unknown";
    }
}

const char *ai_lifetime_status_name(ai_lifetime_status status) {
    switch (status) {
        case AI_LIFETIME_OK: return "lifetime ok";
        case AI_LIFETIME_ERR_INVALID_ARGUMENT: return "lifetime invalid argument";
        case AI_LIFETIME_ERR_NOT_BACKED: return "tensor not backed";
        case AI_LIFETIME_ERR_POLICY_DENIED: return "lifetime policy denied reclaim";
        case AI_LIFETIME_ERR_PINNED: return "tensor or backing pinned";
        case AI_LIFETIME_ERR_SHARED_BUSY: return "shared tensor still has multiple references";
        case AI_LIFETIME_ERR_UNBIND_FAILED: return "tensor backing unbind failed";
        default: return "lifetime unknown error";
    }
}
