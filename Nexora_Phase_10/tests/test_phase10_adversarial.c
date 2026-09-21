#include <assert.h>
#include <stdio.h>
#include "nexora/phase10/phase10.h"

typedef struct exec_trace {
    unsigned calls;
    nx_recovery_action_t last_action;
} exec_trace_t;

static nx_p10_status_t fake_executor(nx_recovery_action_t action,
                                     nx_p10_id_t subject_id,
                                     uint64_t checkpoint_id,
                                     void *opaque)
{
    exec_trace_t *t = (exec_trace_t *)opaque;
    (void)subject_id;
    (void)checkpoint_id;
    ++t->calls;
    t->last_action = action;
    return NX_P10_OK;
}

static void setup_health(nx_health_registry_t *h, nx_p10_id_t id)
{
    nx_health_policy_t hp = { .degrade_after = 10, .fail_after = 20,
                              .max_consecutive_failures = 3 };
    nx_health_init(h, hp);
    assert(nx_health_register(h, id, NX_COMPONENT_SERVICE, 0) == NX_P10_OK);
    assert(nx_health_set_state(h, id, NX_HEALTH_FAILED) == NX_P10_OK);
}

static void test_crafted_capability_cannot_bypass(void)
{
    nx_health_registry_t h;
    nx_checkpoint_store_t c;
    nx_audit_log_t a;
    nx_recovery_engine_t e;
    exec_trace_t trace = {0};
    nx_recovery_policy_t rp = {1, 1, 1, 4};
    nx_security_context_t weak = {
        .principal_id = 1,
        .capabilities = NX_CAP_RESTART_COMPONENT
    };
    nx_recovery_decision_t forged = {
        .action = NX_RECOVERY_PANIC,
        .subject_id = 99,
        .checkpoint_id = 0,
        .required_capability = 0
    };

    setup_health(&h, 99);
    nx_checkpoint_init(&c);
    nx_audit_init(&a);
    nx_recovery_init(&e, rp, &h, &c, &a, fake_executor, &trace);
    assert(nx_recovery_execute(&e, &weak, 1, forged) == NX_P10_EPERM);
    assert(trace.calls == 0);
}

static void test_missing_executor_is_not_false_success(void)
{
    nx_health_registry_t h;
    nx_checkpoint_store_t c;
    nx_audit_log_t a;
    nx_recovery_engine_t e;
    nx_recovery_policy_t rp = {1, 1, 1, 4};
    nx_security_context_t admin = {
        .principal_id = 2,
        .capabilities = NX_CAP_RESTART_COMPONENT
    };
    nx_recovery_decision_t d = {
        .action = NX_RECOVERY_RESTART,
        .subject_id = 7,
        .required_capability = 0
    };

    setup_health(&h, 7);
    nx_checkpoint_init(&c);
    nx_audit_init(&a);
    nx_recovery_init(&e, rp, &h, &c, &a, NULL, NULL);
    assert(nx_recovery_execute(&e, &admin, 2, d) == NX_P10_ESTATE);
    assert(nx_health_get(&h, 7)->state == NX_HEALTH_FAILED);
}

static void test_rollback_after_is_effective(void)
{
    nx_health_registry_t h;
    nx_checkpoint_store_t c;
    nx_audit_log_t a;
    nx_recovery_engine_t e;
    exec_trace_t trace = {0};
    nx_recovery_policy_t rp = {
        .retry_limit = 1, .restart_limit = 5,
        .rollback_after = 2, .quarantine_after = 8
    };
    uint64_t ckpt;
    nx_recovery_decision_t d;

    setup_health(&h, 42);
    nx_checkpoint_init(&c);
    nx_audit_init(&a);
    assert(nx_checkpoint_prepare(&c, 42, 3, 0x1234, 10, &ckpt) == NX_P10_OK);
    assert(nx_checkpoint_commit(&c, ckpt) == NX_P10_OK);
    nx_recovery_init(&e, rp, &h, &c, &a, fake_executor, &trace);
    nx_health_get(&h, 42)->recovery_attempts = 2;
    d = nx_recovery_decide(&e, 42, NX_FAILURE_PROCESS);
    assert(d.action == NX_RECOVERY_ROLLBACK);
    assert(d.checkpoint_id == ckpt);
}

static void test_invalid_rollback_checkpoint_rejected(void)
{
    nx_health_registry_t h;
    nx_checkpoint_store_t c;
    nx_audit_log_t a;
    nx_recovery_engine_t e;
    exec_trace_t trace = {0};
    nx_recovery_policy_t rp = {1, 1, 1, 4};
    nx_security_context_t admin = {
        .principal_id = 2,
        .capabilities = NX_CAP_ROLLBACK
    };
    nx_recovery_decision_t forged = {
        .action = NX_RECOVERY_ROLLBACK,
        .subject_id = 15,
        .checkpoint_id = 0xDEADBEEF,
        .required_capability = NX_CAP_ROLLBACK
    };

    setup_health(&h, 15);
    nx_checkpoint_init(&c);
    nx_audit_init(&a);
    nx_recovery_init(&e, rp, &h, &c, &a, fake_executor, &trace);
    assert(nx_recovery_execute(&e, &admin, 3, forged) == NX_P10_ECORRUPT);
    assert(trace.calls == 0);
}

static void test_prepared_checkpoints_are_not_evicted(void)
{
    nx_checkpoint_store_t c;
    uint64_t id;
    size_t i;
    nx_checkpoint_init(&c);
    for (i = 0; i < NX_P10_MAX_CHECKPOINTS; ++i)
        assert(nx_checkpoint_prepare(&c, 1, (uint64_t)i, (uint64_t)i,
                                     (nx_p10_time_t)i, &id) == NX_P10_OK);
    assert(nx_checkpoint_prepare(&c, 1, 999, 999, 999, &id) == NX_P10_ENOSPC);
}

static void test_equal_score_tie_breaks_by_node_id(void)
{
    nx_node_table_t t;
    nx_node_health_t high_id = {
        .node_id = 9, .load_milli = 100, .memory_pressure_milli = 100,
        .accelerator_pressure_milli = 100, .link_cost_milli = 100,
        .reachable = 1, .in_use = 1
    };
    nx_node_health_t low_id = high_id;
    const nx_node_health_t *best;
    low_id.node_id = 4;
    nx_node_table_init(&t);
    assert(nx_node_upsert(&t, &high_id) == NX_P10_OK);
    assert(nx_node_upsert(&t, &low_id) == NX_P10_OK);
    best = nx_node_select_recovery_target(&t, 0);
    assert(best && best->node_id == 4);
}

static void test_health_generation_tracks_heartbeat_transition(void)
{
    nx_health_registry_t h;
    nx_health_policy_t hp = {10, 20, 3};
    uint32_t generation;
    nx_health_init(&h, hp);
    assert(nx_health_register(&h, 5, NX_COMPONENT_RUNTIME, 0) == NX_P10_OK);
    generation = nx_health_get(&h, 5)->generation;
    assert(nx_health_heartbeat(&h, 5, 1, 0) == NX_P10_OK);
    assert(nx_health_get(&h, 5)->state == NX_HEALTH_DEGRADED);
    assert(nx_health_get(&h, 5)->generation == generation + 1u);
}

static void test_audit_wrap_order(void)
{
    nx_audit_log_t a;
    nx_audit_event_t out[4];
    size_t i, n;
    nx_audit_init(&a);
    for (i = 0; i < NX_P10_AUDIT_CAPACITY + 3u; ++i)
        nx_audit_write(&a, (nx_p10_time_t)i, NX_AUDIT_RECOVERY_RESULT,
                       1, 2, 0, i, 0);
    n = nx_audit_snapshot(&a, out, 4);
    assert(n == 4);
    assert(out[0].seq + 1u == out[1].seq);
    assert(out[1].seq + 1u == out[2].seq);
    assert(out[2].seq + 1u == out[3].seq);
    assert(out[3].seq == NX_P10_AUDIT_CAPACITY + 3u);
}

int main(void)
{
    test_crafted_capability_cannot_bypass();
    test_missing_executor_is_not_false_success();
    test_rollback_after_is_effective();
    test_invalid_rollback_checkpoint_rejected();
    test_prepared_checkpoints_are_not_evicted();
    test_equal_score_tie_breaks_by_node_id();
    test_health_generation_tracks_heartbeat_transition();
    test_audit_wrap_order();
    puts("Phase 10 adversarial tests: PASS");
    return 0;
}
