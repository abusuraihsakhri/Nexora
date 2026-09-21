#include <ai/obs_reliability.h>

static ai_obs_severity obs_rel_severity(const ai_rel_event *e) {
    if (e == NULL) {
        return AI_OBS_SEV_INFO;
    }
    if (e->kind == AI_REL_EVENT_FAULT) {
        if (e->arg0 >= (u64)AI_REL_SEV_FATAL) {
            return AI_OBS_SEV_FATAL;
        }
        if (e->arg0 >= (u64)AI_REL_SEV_ERROR) {
            return AI_OBS_SEV_ERROR;
        }
        if (e->arg0 >= (u64)AI_REL_SEV_WARNING) {
            return AI_OBS_SEV_WARNING;
        }
    }
    if (e->kind == AI_REL_EVENT_SAFE_MODE_ENTER || e->kind == AI_REL_EVENT_HEALTH_TRANSITION) {
        return AI_OBS_SEV_WARNING;
    }
    return AI_OBS_SEV_INFO;
}

static ai_obs_visibility obs_rel_visibility(const ai_rel_event *e) {
    if (e != NULL && e->kind == AI_REL_EVENT_FAULT &&
        (e->arg1 == (u64)AI_REL_FAULT_CAPABILITY_VIOLATION ||
         e->arg1 == (u64)AI_REL_FAULT_INTERNAL_INVARIANT)) {
        return AI_OBS_VIS_SENSITIVE;
    }
    return AI_OBS_VIS_OPERATOR;
}

void ai_obs_rel_cursor_init(ai_obs_rel_cursor *cursor) {
    if (cursor == NULL) {
        return;
    }
    cursor->next_sequence = 0ull;
    cursor->source_gaps = 0ull;
    cursor->mirrored_events = 0ull;
}

u32 ai_obs_mirror_reliability(
    ai_observability *obs,
    const ai_reliability *rel,
    ai_obs_rel_cursor *cursor,
    u32 max_events,
    u64 now_ns
) {
    ai_rel_event oldest;
    u32 i;
    u32 mirrored = 0u;

    if (obs == NULL || rel == NULL || cursor == NULL || max_events == 0u) {
        return 0u;
    }
    if (ai_rel_journal_count(rel) == 0u || !ai_rel_journal_get_oldest(rel, 0u, &oldest)) {
        return 0u;
    }

    if (cursor->next_sequence == 0ull) {
        cursor->next_sequence = oldest.sequence;
    } else if (cursor->next_sequence < oldest.sequence) {
        u64 lost = oldest.sequence - cursor->next_sequence;
        cursor->source_gaps += lost;
        ai_obs_trace_append(
            obs,
            AI_OBS_TRACE_SOURCE_GAP,
            AI_OBS_SEV_WARNING,
            AI_OBS_VIS_OPERATOR,
            0ull,
            0ull,
            0ull,
            0ull,
            cursor->next_sequence,
            oldest.sequence,
            now_ns
        );
        cursor->next_sequence = oldest.sequence;
    }

    for (i = 0u; i < ai_rel_journal_count(rel) && mirrored < max_events; ++i) {
        ai_rel_event e;
        if (!ai_rel_journal_get_oldest(rel, i, &e)) {
            break;
        }
        if (e.sequence < cursor->next_sequence) {
            continue;
        }
        ai_obs_trace_append(
            obs,
            AI_OBS_TRACE_RELIABILITY,
            obs_rel_severity(&e),
            obs_rel_visibility(&e),
            0ull,
            e.sequence,
            e.actor_id,
            e.object_id,
            e.arg0,
            e.arg1,
            e.timestamp_ns
        );
        cursor->next_sequence = e.sequence + 1ull;
        cursor->mirrored_events++;
        mirrored++;
    }
    return mirrored;
}
