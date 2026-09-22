#include <stdio.h>
#include <stdlib.h>
#include <ai/telemetry.h>
#include <ai/fault_injection.h>
#include <ai/release_health.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static int test_telemetry(void) {
    ai_telemetry_buffer b;
    ai_telemetry_event e;
    ai_telemetry_init(&b);
    CHECK(ai_telemetry_count(&b) == 0);
    CHECK(ai_telemetry_count(NULL) == 0);
    CHECK(ai_telemetry_dropped(NULL) == 0);
    CHECK(!ai_telemetry_get_oldest(NULL, 0, &e));
    CHECK(!ai_telemetry_get_oldest(&b, 0, NULL));

    for (u32 i = 0; i < AI_TELEMETRY_CAPACITY + 7U; ++i) {
        ai_telemetry_emit(&b, 1000U + i, AI_TELEM_WORK_DISPATCH, 0, i, i * 2U, 0);
    }
    CHECK(ai_telemetry_count(&b) == AI_TELEMETRY_CAPACITY);
    CHECK(ai_telemetry_dropped(&b) == 7U);
    CHECK(ai_telemetry_get_oldest(&b, 0, &e));
    CHECK(e.object_id == 7U);
    CHECK(e.seq == 8U);
    CHECK(ai_telemetry_get_oldest(&b, AI_TELEMETRY_CAPACITY - 1U, &e));
    CHECK(e.object_id == AI_TELEMETRY_CAPACITY + 6U);
    CHECK(e.seq == AI_TELEMETRY_CAPACITY + 7U);
    CHECK(!ai_telemetry_get_oldest(&b, AI_TELEMETRY_CAPACITY, &e));
    return 0;
}

static int test_faults(void) {
    ai_fault_injector f;
    ai_fault_init(&f);
    CHECK(!ai_fault_configure(NULL, AI_FAULT_DMA, AI_FAULT_ALWAYS, 0));
    CHECK(!ai_fault_configure(&f, (ai_fault_point)0, AI_FAULT_ALWAYS, 0));
    CHECK(!ai_fault_configure(&f, AI_FAULT_DMA, AI_FAULT_DISABLED, 0));
    CHECK(!ai_fault_configure(&f, AI_FAULT_DMA, (ai_fault_mode)99, 0));
    CHECK(!ai_fault_configure(&f, AI_FAULT_DMA, AI_FAULT_EVERY_N, 0));

    CHECK(ai_fault_configure(&f, AI_FAULT_DMA, AI_FAULT_EVERY_N, 3));
    CHECK(!ai_fault_should_fail(&f, AI_FAULT_DMA));
    CHECK(!ai_fault_should_fail(&f, AI_FAULT_DMA));
    CHECK(ai_fault_should_fail(&f, AI_FAULT_DMA));
    CHECK(ai_fault_fired_count(&f, AI_FAULT_DMA) == 1U);

    CHECK(ai_fault_configure(&f, AI_FAULT_TIMEOUT, AI_FAULT_ONCE, 123));
    CHECK(ai_fault_should_fail(&f, AI_FAULT_TIMEOUT));
    CHECK(!ai_fault_should_fail(&f, AI_FAULT_TIMEOUT));
    CHECK(ai_fault_fired_count(&f, AI_FAULT_TIMEOUT) == 1U);

    CHECK(ai_fault_configure(&f, AI_FAULT_MAP, AI_FAULT_ALWAYS, 0));
    CHECK(ai_fault_should_fail(&f, AI_FAULT_MAP));
    CHECK(ai_fault_should_fail(&f, AI_FAULT_MAP));

    ai_fault_disable(&f, AI_FAULT_DMA);
    CHECK(!ai_fault_should_fail(&f, AI_FAULT_DMA));
    CHECK(ai_fault_fired_count(NULL, AI_FAULT_DMA) == 0U);
    return 0;
}

static int test_health(void) {
    ai_health_registry r;
    ai_health_init(&r);
    CHECK(!ai_health_release_ready(NULL));
    CHECK(ai_health_register(&r, 1, "allocator", true));
    CHECK(ai_health_register(&r, 2, "remote", false));
    CHECK(!ai_health_register(&r, 1, "duplicate", true));
    CHECK(!ai_health_register(&r, 3, NULL, true));
    CHECK(!ai_health_release_ready(&r));
    CHECK(!ai_health_set(&r, 999, AI_HEALTH_PASS, 0));
    CHECK(!ai_health_set(&r, 1, (ai_health_status)99, 0));
    CHECK(ai_health_set(&r, 1, AI_HEALTH_PASS, 0));
    CHECK(ai_health_set(&r, 2, AI_HEALTH_WARN, 1));
    CHECK(ai_health_release_ready(&r));

    ai_health_summary s = ai_health_summarize(&r);
    CHECK(s.pass == 1U && s.warn == 1U && s.fail == 0U && s.unknown == 0U);

    CHECK(ai_health_set(&r, 2, AI_HEALTH_FAIL, 2));
    CHECK(ai_health_release_ready(&r));
    s = ai_health_summarize(&r);
    CHECK(s.fail == 1U && s.critical_fail == 0U);

    CHECK(ai_health_set(&r, 1, AI_HEALTH_FAIL, 3));
    CHECK(!ai_health_release_ready(&r));
    s = ai_health_summarize(&r);
    CHECK(s.critical_fail == 1U);
    return 0;
}

int main(void) {
    if (test_telemetry()) return 1;
    if (test_faults()) return 1;
    if (test_health()) return 1;
    puts("phase16 unit tests: PASS");
    return 0;
}
