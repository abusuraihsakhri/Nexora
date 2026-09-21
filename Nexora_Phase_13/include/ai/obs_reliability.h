#ifndef NEXORA_AI_OBS_RELIABILITY_H
#define NEXORA_AI_OBS_RELIABILITY_H

#include <ai/observability.h>
#include <ai/reliability.h>

typedef struct {
    u64 next_sequence;
    u64 source_gaps;
    u64 mirrored_events;
} ai_obs_rel_cursor;

void ai_obs_rel_cursor_init(ai_obs_rel_cursor *cursor);

/*
 * Pull Phase 12 reliability journal records into the Phase 13 trace ring.
 * max_events bounds work per call. Source journal loss is made explicit through
 * AI_OBS_TRACE_SOURCE_GAP and cursor.source_gaps.
 */
u32 ai_obs_mirror_reliability(
    ai_observability *obs,
    const ai_reliability *rel,
    ai_obs_rel_cursor *cursor,
    u32 max_events,
    u64 now_ns
);

#endif
