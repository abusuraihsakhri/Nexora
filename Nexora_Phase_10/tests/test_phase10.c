#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "nexora/phase10/phase10.h"

typedef struct exec_trace {
    unsigned calls;
    nx_recovery_action_t last_action;
    nx_p10_id_t last_subject;
    uint64_t last_checkpoint;
    int forced_failure;
} exec_trace_t;

static nx_p10_status_t fake_executor(nx_recovery_action_t action,
                                     nx_p10_id_t subject_id,
                                     uint64_t checkpoint_id,
                                     void *opaque)
{
    exec_trace_t *t = (exec_trace_t *)opaque;
    ++t->calls;
    t->last_action = action;
    t->last_subject = subject_id;
    t->last_checkpoint = checkpoint_id;
    return t->forced_failure ? NX_P10_ESTATE : NX_P10_OK;
}

static void test_health(void)
{
    nx_health_registry_t r;
    nx_health_policy_t p = { .degrade_after = 10, .fail_after = 20, .max_consecutive_failures = 3 };
    nx_health_init(&r, p);
    assert(nx_health_register(&r, 7, NX_COMPONENT_RUNTIME, 100) == NX_P10_OK);
    assert(nx_health_get(&r, 7)->state == NX_HEALTH_HEALTHY);
    assert(nx_health_scan(&r, 111) == 1);
    assert(nx_health_get(&r, 7)->state == NX_HEALTH_DEGRADED);
    assert(nx_health_scan(&r, 121) == 1);
    assert(nx_health_get(&r, 7)->state == NX_HEALTH_FAILED);
    assert(nx_health_heartbeat(&r, 7, 122, 1) == NX_P10_OK);
    assert(nx_health_get(&r, 7)->state == NX_HEALTH_HEALTHY);
}

static void test_checkpoint(void)
{
    nx_checkpoint_store_t s;
    uint64_t id1, id2;
    const nx_checkpoint_t *latest;
    nx_checkpoint_init(&s);
    assert(nx_checkpoint_prepare(&s, 42, 1, 0x1111, 10, &id1) == NX_P10_OK);
    assert(nx_checkpoint_commit(&s, id1) == NX_P10_OK);
    assert(nx_checkpoint_prepare(&s, 42, 2, 0x2222, 20, &id2) == NX_P10_OK);
    assert(nx_checkpoint_commit(&s, id2) == NX_P10_OK);
    latest = nx_checkpoint_latest(&s, 42);
    assert(latest && latest->checkpoint_id == id2);
    ((nx_checkpoint_t *)latest)->payload_tag ^= 1;
    assert(nx_checkpoint_verify(latest) == 0);
    assert(nx_checkpoint_latest(&s, 42)->checkpoint_id == id1);
}

static void test_recovery_security_audit(void)
{
    nx_health_registry_t h;
    nx_checkpoint_store_t c;
    nx_audit_log_t a;
    nx_recovery_engine_t e;
    exec_trace_t trace = {0};
    nx_health_policy_t hp = { 10, 20, 3 };
    nx_recovery_policy_t rp = { .retry_limit = 1, .restart_limit = 1, .rollback_after = 1, .quarantine_after = 4 };
    nx_security_context_t denied = { .principal_id = 1, .capabilities = 0 };
    nx_security_context_t admin = {
        .principal_id = 2,
        .capabilities = NX_CAP_RESTART_COMPONENT | NX_CAP_ROLLBACK |
                        NX_CAP_MIGRATE | NX_CAP_QUARANTINE | NX_CAP_SYSTEM_PANIC
    };
    nx_recovery_decision_t d;
    nx_audit_event_t events[16];
    size_t n;

    nx_health_init(&h, hp);
    nx_checkpoint_init(&c);
    nx_audit_init(&a);
    nx_recovery_init(&e, rp, &h, &c, &a, fake_executor, &trace);
    assert(nx_health_register(&h, 9, NX_COMPONENT_SERVICE, 0) == NX_P10_OK);
    assert(nx_health_set_state(&h, 9, NX_HEALTH_FAILED) == NX_P10_OK);

    d = nx_recovery_decide(&e, 9, NX_FAILURE_PROCESS);
    assert(d.action == NX_RECOVERY_RESTART);
    assert(nx_recovery_execute(&e, &denied, 1, d) == NX_P10_EPERM);
    assert(trace.calls == 0);
    assert(nx_recovery_execute(&e, &admin, 2, d) == NX_P10_OK);
    assert(trace.calls == 1 && trace.last_action == NX_RECOVERY_RESTART);
    assert(nx_health_get(&h, 9)->state == NX_HEALTH_HEALTHY);

    n = nx_audit_snapshot(&a, events, 16);
    assert(n >= 3);
    assert(events[0].type == NX_AUDIT_SECURITY_DENIAL);
}

static void test_watchdog(void)
{
    nx_health_registry_t h;
    nx_checkpoint_store_t c;
    nx_audit_log_t a;
    nx_recovery_engine_t e;
    nx_watchdog_t w;
    exec_trace_t trace = {0};
    nx_health_policy_t hp = { 5, 10, 3 };
    nx_recovery_policy_t rp = { 1, 2, 1, 5 };
    nx_security_context_t admin = {
        .principal_id = 10,
        .capabilities = NX_CAP_RESTART_COMPONENT | NX_CAP_ROLLBACK |
                        NX_CAP_MIGRATE | NX_CAP_QUARANTINE
    };
    nx_health_init(&h, hp);
    nx_checkpoint_init(&c);
    nx_audit_init(&a);
    nx_recovery_init(&e, rp, &h, &c, &a, fake_executor, &trace);
    nx_watchdog_init(&w, &e, NX_FAILURE_PROCESS);
    assert(nx_health_register(&h, 55, NX_COMPONENT_RUNTIME, 100) == NX_P10_OK);
    assert(nx_watchdog_tick(&w, &admin, 111) == 1);
    assert(trace.calls == 1);
    assert(nx_health_get(&h, 55)->state == NX_HEALTH_HEALTHY);
}

static void test_distributed_target(void)
{
    nx_node_table_t t;
    nx_node_health_t a = { .node_id=1, .load_milli=800, .memory_pressure_milli=700,
        .accelerator_pressure_milli=900, .link_cost_milli=50, .reachable=1, .in_use=1 };
    nx_node_health_t b = { .node_id=2, .load_milli=300, .memory_pressure_milli=250,
        .accelerator_pressure_milli=200, .link_cost_milli=40, .reachable=1, .in_use=1 };
    nx_node_health_t c = { .node_id=3, .load_milli=100, .memory_pressure_milli=100,
        .accelerator_pressure_milli=100, .link_cost_milli=10, .reachable=0, .in_use=1 };
    const nx_node_health_t *best;
    nx_node_table_init(&t);
    assert(nx_node_upsert(&t, &a) == NX_P10_OK);
    assert(nx_node_upsert(&t, &b) == NX_P10_OK);
    assert(nx_node_upsert(&t, &c) == NX_P10_OK);
    best = nx_node_select_recovery_target(&t, 1);
    assert(best && best->node_id == 2);
}

int main(void)
{
    test_health();
    test_checkpoint();
    test_recovery_security_audit();
    test_watchdog();
    test_distributed_target();
    puts("Phase 10 tests: PASS");
    return 0;
}
