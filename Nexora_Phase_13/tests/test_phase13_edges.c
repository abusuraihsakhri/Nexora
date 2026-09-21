#include <stdio.h>
#include <stdlib.h>
#include <ai/observability.h>
#include <ai/obs_reliability.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL:%s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return EXIT_FAILURE; \
    } \
} while (0)

static int test_registration_edges(void) {
    ai_observability obs;
    u64 bad_bounds[3] = {10ull, 10ull, 20ull};
    u64 good_bounds[2] = {1ull, 2ull};
    u32 i;

    ai_obs_init(&obs, 0ull);
    CHECK(ai_obs_metric_register(&obs, 0u, AI_OBS_METRIC_COUNTER, AI_OBS_UNIT_COUNT,
                                 AI_OBS_SCOPE_SYSTEM, 0ull, AI_OBS_VIS_PUBLIC,
                                 NULL, 0u, 1ull) == 0u);
    CHECK(ai_obs_metric_register(&obs, 1u, AI_OBS_METRIC_HISTOGRAM, AI_OBS_UNIT_COUNT,
                                 AI_OBS_SCOPE_SYSTEM, 0ull, AI_OBS_VIS_PUBLIC,
                                 bad_bounds, 3u, 1ull) == 0u);
    CHECK(ai_obs_metric_register(&obs, 1u, AI_OBS_METRIC_COUNTER, AI_OBS_UNIT_COUNT,
                                 AI_OBS_SCOPE_SYSTEM, 0ull, AI_OBS_VIS_PUBLIC,
                                 good_bounds, 2u, 1ull) == 0u);
    CHECK(ai_obs_metric_register(&obs, 1u, (ai_obs_metric_kind)99, AI_OBS_UNIT_COUNT,
                                 AI_OBS_SCOPE_SYSTEM, 0ull, AI_OBS_VIS_PUBLIC,
                                 NULL, 0u, 1ull) == 0u);
    CHECK(ai_obs_metric_register(&obs, 1u, AI_OBS_METRIC_COUNTER, (ai_obs_unit)99,
                                 AI_OBS_SCOPE_SYSTEM, 0ull, AI_OBS_VIS_PUBLIC,
                                 NULL, 0u, 1ull) == 0u);

    CHECK(ai_obs_metric_register(&obs, 77u, AI_OBS_METRIC_COUNTER, AI_OBS_UNIT_COUNT,
                                 AI_OBS_SCOPE_SYSTEM, 9ull, AI_OBS_VIS_PUBLIC,
                                 NULL, 0u, 1ull) != 0u);
    CHECK(ai_obs_metric_register(&obs, 77u, AI_OBS_METRIC_COUNTER, AI_OBS_UNIT_COUNT,
                                 AI_OBS_SCOPE_SYSTEM, 9ull, AI_OBS_VIS_PUBLIC,
                                 NULL, 0u, 1ull) == 0u);

    for (i = 1u; i < AI_OBS_MAX_METRICS; ++i) {
        CHECK(ai_obs_metric_register(&obs, 100u + i, AI_OBS_METRIC_COUNTER, AI_OBS_UNIT_COUNT,
                                     AI_OBS_SCOPE_SYSTEM, 0ull, AI_OBS_VIS_PUBLIC,
                                     NULL, 0u, (u64)i) != 0u);
    }
    CHECK(ai_obs_metric_register(&obs, 999u, AI_OBS_METRIC_COUNTER, AI_OBS_UNIT_COUNT,
                                 AI_OBS_SCOPE_SYSTEM, 0ull, AI_OBS_VIS_PUBLIC,
                                 NULL, 0u, 99ull) == 0u);
    return 0;
}

static int test_rejected_updates_and_saturation(void) {
    ai_observability obs;
    u32 counter;
    const ai_obs_metric *m;

    ai_obs_init(&obs, 0ull);
    counter = ai_obs_metric_register(&obs, 1u, AI_OBS_METRIC_COUNTER, AI_OBS_UNIT_COUNT,
                                     AI_OBS_SCOPE_SYSTEM, 0ull, AI_OBS_VIS_PUBLIC,
                                     NULL, 0u, 0ull);
    CHECK(counter != 0u);
    CHECK(!ai_obs_gauge_set(&obs, counter, 3ull, 1ull));
    CHECK(obs.rejected_metric_updates == 1ull);
    CHECK(ai_obs_counter_add(&obs, counter, ~0ull, 2ull));
    CHECK(ai_obs_counter_add(&obs, counter, 1ull, 3ull));
    m = ai_obs_metric_find_const(&obs, counter);
    CHECK(m != NULL && m->value == ~0ull);
    return 0;
}

static int test_span_capacity_and_clock_regression(void) {
    ai_observability obs;
    u64 ids[AI_OBS_MAX_ACTIVE_SPANS];
    u32 i;

    ai_obs_init(&obs, 0ull);
    CHECK(ai_obs_span_begin(&obs, (ai_obs_span_kind)99, AI_OBS_VIS_PUBLIC,
                            1ull, 1ull, 1ull, 1ull) == 0ull);
    CHECK(obs.rejected_spans == 1ull);
    for (i = 0u; i < AI_OBS_MAX_ACTIVE_SPANS; ++i) {
        ids[i] = ai_obs_span_begin(&obs, AI_OBS_SPAN_WORK, AI_OBS_VIS_PUBLIC,
                                   (u64)i, 1ull, (u64)i, 100ull);
        CHECK(ids[i] != 0ull);
    }
    CHECK(ai_obs_span_begin(&obs, AI_OBS_SPAN_WORK, AI_OBS_VIS_PUBLIC,
                            99ull, 1ull, 1ull, 100ull) == 0ull);
    CHECK(obs.rejected_spans == 2ull);
    CHECK(obs.span_registry.peak_active == AI_OBS_MAX_ACTIVE_SPANS);

    CHECK(ai_obs_span_end(&obs, ids[0], 0u, 0ull, 90ull));
    CHECK(obs.clock_regressions == 1ull);
    CHECK(ai_obs_span_cancel(&obs, ids[1], 7ull, 110ull));
    CHECK(!ai_obs_span_end(&obs, ids[1], 0u, 0ull, 120ull));
    CHECK(obs.rejected_spans == 3ull);
    return 0;
}

static int test_reliability_gap(void) {
    ai_observability obs;
    ai_reliability rel;
    ai_obs_rel_cursor cursor;
    ai_obs_trace_event e;
    bool saw_gap = false;
    u32 i;

    ai_obs_init(&obs, 0ull);
    ai_rel_init(&rel, NULL, 0ull);
    ai_obs_rel_cursor_init(&cursor);
    cursor.next_sequence = 1ull;

    for (i = 0u; i < AI_REL_JOURNAL_CAPACITY + 20u; ++i) {
        ai_rel_journal_append(&rel, AI_REL_EVENT_WORK_START, (u64)i, (u64)i,
                              0ull, 0ull, (u64)i);
    }
    CHECK(rel.journal.dropped_events > 0ull);
    CHECK(ai_obs_mirror_reliability(&obs, &rel, &cursor, 8u, 1000ull) == 8u);
    CHECK(cursor.source_gaps == rel.journal.dropped_events);

    for (i = 0u; i < ai_obs_trace_count(&obs); ++i) {
        CHECK(ai_obs_trace_get_oldest(&obs, i, &e));
        if (e.kind == AI_OBS_TRACE_SOURCE_GAP) {
            saw_gap = true;
        }
    }
    CHECK(saw_gap);
    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_registration_edges();
    rc |= test_rejected_updates_and_saturation();
    rc |= test_span_capacity_and_clock_regression();
    rc |= test_reliability_gap();

    if (rc == 0) {
        puts("Phase 13 edge tests: PASS");
    }
    return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
