#include "nexora/phase17/build_info.h"
#include "nexora/phase17/health.h"
#include "nexora/phase17/trace.h"
#include "nexora/phase17/watchdog.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_health(void) {
    nx_health_reset();
    uint32_t sched = 99u, mem = 99u;
    assert(nx_health_register("scheduler", &sched) == 0);
    assert(nx_health_register("memory", &mem) == 0);
    assert(sched == 0u && mem == 1u);
    assert(nx_health_update(sched, NX_HEALTH_OK, 0, 100u) == 0);
    assert(nx_health_update(mem, NX_HEALTH_DEGRADED, 7, 110u) == 0);
    nx_health_summary_t s = nx_health_summarize();
    assert(s.total == 2u && s.ok == 1u && s.degraded == 1u);
    assert(s.aggregate == NX_HEALTH_DEGRADED);
    nx_health_snapshot_t snap;
    assert(nx_health_get(mem, &snap) == 0);
    assert(strcmp(snap.name, "memory") == 0);
    assert(snap.last_code == 7);
}

static void test_trace(void) {
    nx_trace_reset();
    for (uint64_t i = 0; i < 1100u; ++i) {
        const uint64_t seq = nx_trace_emit(1000u + i, 2u, (uint16_t)(i % 31u), NX_TRACE_INFO, 0u, i, i * 2u);
        assert(seq == i + 1u);
    }
    assert(nx_trace_count() == 1100u);
    nx_trace_event_t latest[32];
    size_t n = nx_trace_copy_latest(latest, 32u);
    assert(n == 32u);
    assert(latest[0].sequence == 1069u);
    assert(latest[31].sequence == 1100u);
    assert(nx_trace_checksum_latest(32u) != 0u);
}

static void test_watchdog(void) {
    nx_watchdog_reset();
    assert(nx_watchdog_arm(4u, 100u, 1000u) == 0);
    nx_watchdog_target_t overdue[4];
    assert(nx_watchdog_check(1099u, overdue, 4u) == 0u);
    assert(nx_watchdog_check(1101u, overdue, 4u) == 1u);
    assert(overdue[0].component_id == 4u);
    assert(overdue[0].misses == 1u);
    assert(nx_watchdog_check(1200u, overdue, 4u) == 1u);
    assert(overdue[0].misses == 1u);
    assert(nx_watchdog_heartbeat(4u, 1200u) == 0);
    assert(nx_watchdog_check(1201u, overdue, 4u) == 0u);
}

static void test_build_info(void) {
    const nx_build_info_t *b = nx_build_info();
    assert(b != NULL);
    assert(strcmp(b->project, "Nexora") == 0);
    assert(strcmp(b->phase, "17") == 0);
}

int main(void) {
    test_health();
    test_trace();
    test_watchdog();
    test_build_info();
    puts("phase17: all C tests passed");
    return 0;
}
