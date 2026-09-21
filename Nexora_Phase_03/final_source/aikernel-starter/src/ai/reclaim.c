#include <ai/reclaim.h>
#include <ai/tensor.h>
#include <ai/work.h>
#include <ai/instrument.h>
#include <kernel/memory_object.h>
#include <kernel/object.h>

typedef struct {
    nx_handle_t tensor_handle;
    ai_reclaim_reason reason;
} deferred_entry;

static deferred_entry deferred[AI_RECLAIM_DEFERRED_CAPACITY];
static u32 deferred_count;
static ai_reclaim_stats stats;
static bool final_consumer_enabled = true;

static bool retryable(ai_lifetime_status status) {
    return status == AI_LIFETIME_ERR_PINNED ||
           status == AI_LIFETIME_ERR_SHARED_BUSY ||
           status == AI_LIFETIME_ERR_UNBIND_FAILED;
}

static bool enqueue_deferred(ai_tensor *tensor, ai_reclaim_reason reason) {
    if (tensor == NULL || tensor->object.handle == NX_INVALID_HANDLE) {
        return false;
    }

    for (u32 i = 0; i < deferred_count; ++i) {
        if (deferred[i].tensor_handle == tensor->object.handle &&
            deferred[i].reason == reason) {
            ++stats.deferred_duplicate_suppressed;
            return true;
        }
    }

    if (deferred_count >= AI_RECLAIM_DEFERRED_CAPACITY) {
        return false;
    }

    deferred[deferred_count].tensor_handle = tensor->object.handle;
    deferred[deferred_count].reason = reason;
    ++deferred_count;
    ++stats.deferred_enqueued;
    return true;
}

void ai_reclaim_system_init(void) {
    deferred_count = 0;
    final_consumer_enabled = true;
    for (u32 i = 0; i < AI_RECLAIM_DEFERRED_CAPACITY; ++i) {
        deferred[i].tensor_handle = NX_INVALID_HANDLE;
        deferred[i].reason = AI_RECLAIM_EXPLICIT;
    }

    stats.final_consumer_events = 0;
    stats.final_consumer_attempts = 0;
    stats.final_consumer_successes = 0;
    stats.final_consumer_deferred = 0;
    stats.deferred_enqueued = 0;
    stats.deferred_duplicate_suppressed = 0;
    stats.deferred_retry_passes = 0;
    stats.deferred_retry_attempts = 0;
    stats.deferred_retry_successes = 0;
    stats.deferred_stale_drops = 0;
    stats.pressure_runs = 0;
    stats.pressure_targets_bytes = 0;
    stats.pressure_binding_bytes_released = 0;
    stats.pressure_resident_bytes_reclaimed = 0;
    stats.pressure_target_met = 0;
    stats.cache_eviction_runs = 0;
    stats.cache_resident_bytes_reclaimed = 0;
}


void ai_reclaim_set_final_consumer_enabled(bool enabled) {
    final_consumer_enabled = enabled;
}

bool ai_reclaim_final_consumer_enabled(void) {
    return final_consumer_enabled;
}

ai_lifetime_status ai_reclaim_on_final_consumer(
    ai_tensor *tensor,
    bool *out_deferred
) {
    if (out_deferred != NULL) {
        *out_deferred = false;
    }
    if (tensor == NULL) {
        return AI_LIFETIME_ERR_INVALID_ARGUMENT;
    }

    ++stats.final_consumer_events;

    if (!final_consumer_enabled) {
        return AI_LIFETIME_ERR_POLICY_DENIED;
    }

    if (tensor->lifetime != AI_TENSOR_LIFETIME_TEMPORARY ||
        !ai_tensor_is_backed(tensor)) {
        return AI_LIFETIME_ERR_POLICY_DENIED;
    }

    ++stats.final_consumer_attempts;
    const ai_lifetime_status status = ai_tensor_lifetime_reclaim(
        tensor, AI_RECLAIM_FINAL_CONSUMER
    );
    if (status == AI_LIFETIME_OK) {
        ++stats.final_consumer_successes;
        return status;
    }

    if (retryable(status) && enqueue_deferred(tensor, AI_RECLAIM_FINAL_CONSUMER)) {
        ++stats.final_consumer_deferred;
        if (out_deferred != NULL) {
            *out_deferred = true;
        }
    }
    return status;
}

static u64 add_saturating(u64 a, u64 b) {
    return a > (~(u64)0) - b ? ~(u64)0 : a + b;
}

static u32 retry_deferred_internal(u64 *out_binding_bytes, u64 *out_resident_bytes) {
    if (out_binding_bytes != NULL) {
        *out_binding_bytes = 0;
    }
    if (out_resident_bytes != NULL) {
        *out_resident_bytes = 0;
    }
    if (deferred_count == 0) {
        return 0;
    }

    ++stats.deferred_retry_passes;
    u32 write = 0;
    u32 reclaimed = 0;

    for (u32 read = 0; read < deferred_count; ++read) {
        const deferred_entry entry = deferred[read];
        ai_tensor *tensor = ai_tensor_lookup(entry.tensor_handle);
        if (tensor == NULL || !ai_tensor_is_backed(tensor)) {
            ++stats.deferred_stale_drops;
            continue;
        }

        const u64 binding = tensor->storage_span_bytes;
        const u64 resident_before = nx_memory_get_stats()->resident_bytes;
        ++stats.deferred_retry_attempts;
        const ai_lifetime_status status = ai_tensor_lifetime_reclaim(
            tensor, entry.reason
        );
        if (status == AI_LIFETIME_OK) {
            const u64 resident_after = nx_memory_get_stats()->resident_bytes;
            ++stats.deferred_retry_successes;
            ++reclaimed;
            if (out_binding_bytes != NULL) {
                *out_binding_bytes = add_saturating(*out_binding_bytes, binding);
            }
            if (out_resident_bytes != NULL && resident_before >= resident_after) {
                *out_resident_bytes = add_saturating(
                    *out_resident_bytes, resident_before - resident_after
                );
            }
            continue;
        }

        if (!retryable(status)) {
            ++stats.deferred_stale_drops;
            continue;
        }

        deferred[write++] = entry;
    }

    for (u32 i = write; i < deferred_count; ++i) {
        deferred[i].tensor_handle = NX_INVALID_HANDLE;
        deferred[i].reason = AI_RECLAIM_EXPLICIT;
    }
    deferred_count = write;
    return reclaimed;
}

u32 ai_reclaim_retry_deferred(void) {
    return retry_deferred_internal(NULL, NULL);
}

u32 ai_reclaim_deferred_count(void) {
    return deferred_count;
}

static bool producer_quiesced(const ai_tensor *tensor) {
    if (tensor->producer_work == NX_INVALID_HANDLE) {
        return true;
    }

    nx_object *object = nx_object_lookup(tensor->producer_work, NX_OBJECT_WORK);
    if (object == NULL) {
        /* A stale producer handle means no future work object can write it. */
        return true;
    }

    const ai_work_node *producer = (const ai_work_node *)object;
    return producer->state == AI_WORK_DONE || producer->state == AI_WORK_FAILED;
}

static bool graph_dead_for_pressure(const ai_tensor *tensor) {
    if (tensor == NULL || tensor->object.state != NX_OBJECT_LIVE ||
        !ai_tensor_is_backed(tensor) || tensor->consumers_remaining != 0) {
        return false;
    }

    if (!producer_quiesced(tensor)) {
        return false;
    }

    if (tensor->lifetime == AI_TENSOR_LIFETIME_TEMPORARY) {
        /*
         * An unattached temporary tensor may still be in active caller use.
         * Require evidence that it participated in a completed graph edge, or
         * that a producer finished and no consumer ever needed the output.
         */
        return tensor->consumer_count != 0 || tensor->producer_work != NX_INVALID_HANDLE;
    }

    /* Cached tensors are explicitly evictable even when they are cache-only. */
    return tensor->lifetime == AI_TENSOR_LIFETIME_CACHED;
}

static u64 candidate_capacity(const ai_tensor *tensor) {
    nx_memory *memory = ai_tensor_backing(tensor);
    return memory != NULL ? memory->capacity_bytes : 0;
}

static ai_tensor *find_best_candidate(
    ai_tensor_lifetime lifetime,
    ai_reclaim_report *report
) {
    ai_tensor *best = NULL;
    u64 best_capacity = 0;

    for (u64 i = 0; i < ai_tensor_count(); ++i) {
        ai_tensor *tensor = ai_tensor_at(i);
        if (report != NULL) {
            ++report->candidates_scanned;
        }
        if (tensor == NULL || tensor->lifetime != lifetime ||
            !ai_tensor_is_backed(tensor)) {
            continue;
        }

        if (!graph_dead_for_pressure(tensor)) {
            if (report != NULL) {
                ++report->skipped_live;
            }
            continue;
        }

        nx_memory *memory = ai_tensor_backing(tensor);
        if ((tensor->flags & AI_TENSOR_PINNED) || tensor->object.pin_count != 0 ||
            (memory != NULL && memory->object.pin_count != 0)) {
            if (report != NULL) {
                ++report->skipped_pinned;
            }
            continue;
        }

        const u64 capacity = candidate_capacity(tensor);
        if (best == NULL || capacity > best_capacity ||
            (capacity == best_capacity && tensor->object.id < best->object.id)) {
            best = tensor;
            best_capacity = capacity;
        }
    }

    return best;
}

static ai_reclaim_status reclaim_pass(
    u64 target_resident_bytes,
    bool cache_only,
    ai_reclaim_report *out_report
) {
    if (out_report == NULL || target_resident_bytes == 0) {
        return AI_RECLAIM_ERR_INVALID_ARGUMENT;
    }

    ai_reclaim_report report;
    report.target_resident_bytes = target_resident_bytes;
    report.resident_before = nx_memory_get_stats()->resident_bytes;
    report.resident_after = report.resident_before;
    report.binding_bytes_released = 0;
    report.resident_bytes_reclaimed = 0;
    report.candidates_scanned = 0;
    report.attempts = 0;
    report.successes = 0;
    report.denied = 0;
    report.skipped_live = 0;
    report.skipped_pinned = 0;
    report.target_met = false;

    /* A pressure event is also a natural retry point for deferred final-use frees. */
    u64 deferred_binding = 0;
    u64 deferred_resident = 0;
    (void)retry_deferred_internal(&deferred_binding, &deferred_resident);
    report.binding_bytes_released = deferred_binding;
    report.resident_bytes_reclaimed = deferred_resident;

    const ai_tensor_lifetime phases[2] = {
        AI_TENSOR_LIFETIME_TEMPORARY,
        AI_TENSOR_LIFETIME_CACHED
    };
    const u32 phase_start = cache_only ? 1u : 0u;

    for (u32 phase = phase_start; phase < 2u; ++phase) {
        const ai_tensor_lifetime lifetime = phases[phase];
        for (;;) {
            const u64 resident_now = nx_memory_get_stats()->resident_bytes;
            if (report.resident_before >= resident_now) {
                report.resident_bytes_reclaimed = report.resident_before - resident_now;
            }
            if (report.resident_bytes_reclaimed >= target_resident_bytes) {
                report.target_met = true;
                break;
            }

            ai_tensor *candidate = find_best_candidate(lifetime, &report);
            if (candidate == NULL) {
                break;
            }

            const u64 binding = candidate->storage_span_bytes;
            const u64 before = nx_memory_get_stats()->resident_bytes;
            ++report.attempts;
            const ai_lifetime_status status = ai_tensor_lifetime_reclaim(
                candidate,
                cache_only ? AI_RECLAIM_CACHE_EVICTION : AI_RECLAIM_MEMORY_PRESSURE
            );
            const u64 after = nx_memory_get_stats()->resident_bytes;

            if (status == AI_LIFETIME_OK) {
                ++report.successes;
                report.binding_bytes_released += binding;
                if (before >= after) {
                    report.resident_bytes_reclaimed += before - after;
                }
            } else {
                ++report.denied;
                /* Avoid repeatedly selecting a permanently denied object. */
                if (!retryable(status)) {
                    break;
                }
                /* Retryable candidates are skipped for this pressure pass. */
                break;
            }
        }

        if (report.target_met) {
            break;
        }
    }

    report.resident_after = nx_memory_get_stats()->resident_bytes;
    if (report.resident_before >= report.resident_after) {
        report.resident_bytes_reclaimed = report.resident_before - report.resident_after;
    }
    report.target_met = report.resident_bytes_reclaimed >= target_resident_bytes;
    *out_report = report;

    if (report.target_met) {
        return AI_RECLAIM_OK;
    }
    return report.successes == 0
        ? AI_RECLAIM_ERR_NO_CANDIDATE
        : AI_RECLAIM_ERR_TARGET_NOT_MET;
}

ai_reclaim_status ai_reclaim_under_pressure(
    u64 target_resident_bytes,
    ai_reclaim_report *out_report
) {
    if (out_report == NULL || target_resident_bytes == 0) {
        return AI_RECLAIM_ERR_INVALID_ARGUMENT;
    }

    ++stats.pressure_runs;
    stats.pressure_targets_bytes += target_resident_bytes;

    const ai_reclaim_status status = reclaim_pass(
        target_resident_bytes, false, out_report
    );
    stats.pressure_binding_bytes_released += out_report->binding_bytes_released;
    stats.pressure_resident_bytes_reclaimed += out_report->resident_bytes_reclaimed;
    if (out_report->target_met) {
        ++stats.pressure_target_met;
    }
    return status;
}

ai_reclaim_status ai_reclaim_evict_cache(
    u64 target_resident_bytes,
    ai_reclaim_report *out_report
) {
    if (out_report == NULL || target_resident_bytes == 0) {
        return AI_RECLAIM_ERR_INVALID_ARGUMENT;
    }

    ++stats.cache_eviction_runs;
    const ai_reclaim_status status = reclaim_pass(
        target_resident_bytes, true, out_report
    );
    stats.cache_resident_bytes_reclaimed += out_report->resident_bytes_reclaimed;
    return status;
}

const ai_reclaim_stats *ai_reclaim_get_stats(void) {
    return &stats;
}

const char *ai_reclaim_status_name(ai_reclaim_status status) {
    switch (status) {
        case AI_RECLAIM_OK: return "reclaim ok";
        case AI_RECLAIM_ERR_INVALID_ARGUMENT: return "reclaim invalid argument";
        case AI_RECLAIM_ERR_NO_CANDIDATE: return "reclaim no eligible candidate";
        case AI_RECLAIM_ERR_TARGET_NOT_MET: return "reclaim target not met";
        default: return "reclaim unknown error";
    }
}
