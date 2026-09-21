#include <stdio.h>
#include <stdlib.h>
#include <ai/observability.h>
#include <ai/obs_reliability.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL:%s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static int test_metrics(void) {
    ai_observability obs;
    u64 bounds[4] = {10ull, 100ull, 1000ull, 10000ull};
    u32 counter;
    u32 gauge;
    u32 hist;
    const ai_obs_metric *m;
    ai_obs_metric copy;

    ai_obs_init(&obs, 1ull);
    counter = ai_obs_metric_register(
        &obs, 1001u, AI_OBS_METRIC_COUNTER, AI_OBS_UNIT_COUNT,
        AI_OBS_SCOPE_SCHEDULER, 1ull, AI_OBS_VIS_PUBLIC, NULL, 0u, 2ull
    );
    gauge = ai_obs_metric_register(
        &obs, 1002u, AI_OBS_METRIC_GAUGE, AI_OBS_UNIT_BYTES,
        AI_OBS_SCOPE_DEVICE, 7ull, AI_OBS_VIS_OPERATOR, NULL, 0u, 3ull
    );
    hist = ai_obs_metric_register(
        &obs, 1003u, AI_OBS_METRIC_HISTOGRAM, AI_OBS_UNIT_NANOSECONDS,
        AI_OBS_SCOPE_WORK_GRAPH, 9ull, AI_OBS_VIS_OPERATOR, bounds, 4u, 4ull
    );

    CHECK(counter != 0u && gauge != 0u && hist != 0u);
    CHECK(ai_obs_counter_add(&obs, counter, 3ull, 5ull));
    CHECK(ai_obs_counter_add(&obs, counter, 7ull, 6ull));
    CHECK(ai_obs_gauge_set(&obs, gauge, 4096ull, 7ull));
    CHECK(ai_obs_hist_observe(&obs, hist, 5ull, 8ull));
    CHECK(ai_obs_hist_observe(&obs, hist, 50ull, 9ull));
    CHECK(ai_obs_hist_observe(&obs, hist, 5000ull, 10ull));
    CHECK(ai_obs_hist_observe(&obs, hist, 50000ull, 11ull));

    m = ai_obs_metric_find_const(&obs, counter);
    CHECK(m != NULL && m->value == 10ull && m->updates == 2ull);
    m = ai_obs_metric_find_const(&obs, gauge);
    CHECK(m != NULL && m->value == 4096ull);
    m = ai_obs_metric_find_const(&obs, hist);
    CHECK(m != NULL);
    CHECK(m->hist_count == 4ull);
    CHECK(m->hist_sum == 55055ull);
    CHECK(m->hist_min == 5ull);
    CHECK(m->hist_max == 50000ull);
    CHECK(m->hist_buckets[0] == 1ull);
    CHECK(m->hist_buckets[1] == 1ull);
    CHECK(m->hist_buckets[3] == 1ull);
    CHECK(m->hist_buckets[4] == 1ull);
    CHECK(ai_obs_metric_checksum(&obs) != 0ull);

    CHECK(!ai_obs_metric_read(&obs, gauge, AI_OBS_VIS_PUBLIC, &copy));
    CHECK(ai_obs_metric_read(&obs, gauge, AI_OBS_VIS_OPERATOR, &copy));
    CHECK(copy.value == 4096ull);
    return 0;
}

static int test_trace_visibility_and_ring(void) {
    ai_observability obs;
    ai_obs_trace_event out[8];
    ai_obs_trace_event oldest;
    u64 next = 0ull;
    u32 copied;
    u32 i;

    ai_obs_init(&obs, 0ull);
    ai_obs_trace_append(&obs, AI_OBS_TRACE_CUSTOM, AI_OBS_SEV_INFO, AI_OBS_VIS_PUBLIC,
                        0ull, 1ull, 2ull, 3ull, 4ull, 5ull, 1ull);
    ai_obs_trace_append(&obs, AI_OBS_TRACE_CUSTOM, AI_OBS_SEV_INFO, AI_OBS_VIS_SENSITIVE,
                        0ull, 10ull, 20ull, 30ull, 40ull, 50ull, 2ull);
    ai_obs_trace_append(&obs, AI_OBS_TRACE_CUSTOM, AI_OBS_SEV_INFO, AI_OBS_VIS_OPERATOR,
                        0ull, 11ull, 21ull, 31ull, 41ull, 51ull, 3ull);

    copied = ai_obs_trace_export(&obs, AI_OBS_VIS_PUBLIC, 0ull, out, 8u, &next);
    CHECK(copied == 2u); /* BOOT + public custom */
    CHECK(out[0].kind == AI_OBS_TRACE_BOOT);
    CHECK(out[1].correlation_id == 1ull);
    CHECK(next == obs.trace.next_sequence);

    for (i = 0u; i < AI_OBS_TRACE_CAPACITY + 7u; ++i) {
        ai_obs_trace_append(&obs, AI_OBS_TRACE_EXEC_BEGIN, AI_OBS_SEV_DEBUG, AI_OBS_VIS_PUBLIC,
                            0ull, (u64)i, 0ull, 0ull, 0ull, 0ull, (u64)(100u + i));
    }
    CHECK(ai_obs_trace_count(&obs) == AI_OBS_TRACE_CAPACITY);
    CHECK(obs.trace.dropped_events == 11ull); /* 4 pre-existing + 519 appends => 11 overwritten */
    CHECK(ai_obs_trace_get_oldest(&obs, 0u, &oldest));
    CHECK(oldest.sequence == 12ull);
    CHECK(ai_obs_trace_checksum(&obs) != 0ull);
    return 0;
}

static int test_spans_and_snapshot(void) {
    ai_observability obs;
    u64 bounds[3] = {10ull, 100ull, 1000ull};
    u32 latency;
    u64 span;
    const ai_obs_metric *m;
    ai_obs_snapshot snap;
    u32 before_count;

    ai_obs_init(&obs, 0ull);
    latency = ai_obs_metric_register(
        &obs, 2001u, AI_OBS_METRIC_HISTOGRAM, AI_OBS_UNIT_NANOSECONDS,
        AI_OBS_SCOPE_SCHEDULER, 1ull, AI_OBS_VIS_OPERATOR, bounds, 3u, 1ull
    );
    CHECK(latency != 0u);

    span = ai_obs_span_begin(&obs, AI_OBS_SPAN_SCHEDULER, AI_OBS_VIS_OPERATOR,
                             55ull, 7ull, 8ull, 10ull);
    CHECK(span != 0ull);
    CHECK(obs.span_registry.active_count == 1u);
    CHECK(ai_obs_span_end(&obs, span, latency, 0ull, 70ull));
    CHECK(obs.span_registry.active_count == 0u);
    m = ai_obs_metric_find_const(&obs, latency);
    CHECK(m != NULL && m->hist_count == 1ull && m->hist_sum == 60ull);

    before_count = ai_obs_trace_count(&obs);
    snap = ai_obs_snapshot_capture(&obs, 80ull);
    CHECK(snap.snapshot_id == 1ull);
    CHECK(snap.metric_count == 1u);
    CHECK(snap.active_spans == 0u);
    CHECK(snap.trace_count == before_count);
    CHECK(snap.trace_checksum != 0ull);
    CHECK(snap.metric_checksum != 0ull);
    CHECK(ai_obs_trace_count(&obs) == before_count + 1u);
    {
        ai_obs_trace_event last;
        CHECK(ai_obs_trace_get_oldest(&obs, before_count, &last));
        CHECK(last.kind == AI_OBS_TRACE_DIAGNOSTIC_SNAPSHOT);
        CHECK(last.visibility == AI_OBS_VIS_SENSITIVE);
    }
    return 0;
}

static int test_reliability_mirror(void) {
    ai_observability obs;
    ai_reliability rel;
    ai_obs_rel_cursor cursor;
    ai_rel_domain *d;
    ai_obs_trace_event e;
    u32 mirrored;
    u32 i;
    bool saw_fault = false;

    ai_obs_init(&obs, 0ull);
    ai_rel_init(&rel, NULL, 1ull);
    d = ai_rel_domain_register(&rel, AI_REL_DOMAIN_DEVICE, 44ull, 2ull);
    CHECK(d != NULL);
    CHECK(ai_rel_record_fault(&rel, d->id, AI_REL_SEV_ERROR, AI_REL_FAULT_DMA_ERROR, 3ull));

    ai_obs_rel_cursor_init(&cursor);
    mirrored = ai_obs_mirror_reliability(&obs, &rel, &cursor, 32u, 4ull);
    CHECK(mirrored == ai_rel_journal_count(&rel));
    CHECK(cursor.mirrored_events == mirrored);
    CHECK(cursor.source_gaps == 0ull);
    CHECK(ai_obs_mirror_reliability(&obs, &rel, &cursor, 32u, 5ull) == 0u);

    for (i = 0u; i < ai_obs_trace_count(&obs); ++i) {
        CHECK(ai_obs_trace_get_oldest(&obs, i, &e));
        if (e.kind == AI_OBS_TRACE_RELIABILITY && e.severity == AI_OBS_SEV_ERROR) {
            saw_fault = true;
        }
    }
    CHECK(saw_fault);
    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_metrics();
    rc |= test_trace_visibility_and_ring();
    rc |= test_spans_and_snapshot();
    rc |= test_reliability_mirror();

    if (rc == 0) {
        puts("Phase 13 observability tests: PASS");
    }
    return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
