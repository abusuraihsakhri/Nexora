#include <stdio.h>
#include <stdlib.h>
#include <ai/reliability.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL:%s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static int test_registration_and_journal(void) {
    ai_reliability rel;
    ai_rel_domain *gpu;
    ai_rel_domain *node;
    ai_rel_event e;

    ai_rel_init(&rel, NULL, 10ull);
    gpu = ai_rel_domain_register(&rel, AI_REL_DOMAIN_DEVICE, 7ull, 20ull);
    node = ai_rel_domain_register(&rel, AI_REL_DOMAIN_REMOTE_NODE, 42ull, 30ull);

    CHECK(gpu != NULL);
    CHECK(node != NULL);
    CHECK(gpu->id != node->id);
    CHECK(rel.registry.count == 2u);
    CHECK(ai_rel_journal_count(&rel) == 3u);
    CHECK(ai_rel_journal_get_oldest(&rel, 0u, &e));
    CHECK(e.kind == AI_REL_EVENT_BOOT);
    CHECK(ai_rel_journal_checksum(&rel) != 0ull);
    return 0;
}

static int test_fault_transitions_and_safe_mode(void) {
    ai_reliability rel;
    ai_rel_policy p = ai_rel_default_policy();
    ai_rel_domain *d;

    p.degrade_after_failures = 1u;
    p.quarantine_after_failures = 2u;
    p.fail_after_failures = 4u;
    p.safe_mode_failed_domains = 1u;
    ai_rel_init(&rel, &p, 0ull);
    d = ai_rel_domain_register(&rel, AI_REL_DOMAIN_DEVICE, 11ull, 1ull);
    CHECK(d != NULL);

    CHECK(ai_rel_record_fault(&rel, d->id, AI_REL_SEV_WARNING, AI_REL_FAULT_TIMEOUT, 2ull));
    CHECK(d->health == AI_REL_HEALTH_DEGRADED);
    CHECK(!ai_rel_safe_mode(&rel));

    CHECK(ai_rel_record_fault(&rel, d->id, AI_REL_SEV_ERROR, AI_REL_FAULT_DMA_ERROR, 3ull));
    CHECK(d->health == AI_REL_HEALTH_QUARANTINED);
    CHECK(ai_rel_safe_mode(&rel));
    CHECK(!ai_rel_can_admit(&rel, d->id, true, 4ull));

    CHECK(ai_rel_release_expired_quarantines(&rel, 3ull + p.quarantine_ns) == 1u);
    CHECK(d->health == AI_REL_HEALTH_DEGRADED);
    CHECK(!ai_rel_safe_mode(&rel));
    return 0;
}

static int test_recovery_policy(void) {
    ai_reliability rel;
    ai_rel_policy p = ai_rel_default_policy();
    ai_rel_domain *d;
    ai_rel_recovery_action a;

    p.max_retry_attempts = 1u;
    ai_rel_init(&rel, &p, 0ull);
    d = ai_rel_domain_register(&rel, AI_REL_DOMAIN_DEVICE, 3ull, 1ull);
    CHECK(d != NULL);

    CHECK(ai_rel_record_fault(&rel, d->id, AI_REL_SEV_ERROR, AI_REL_FAULT_DMA_ERROR, 2ull));
    a = ai_rel_choose_recovery(&rel, d->id, AI_REL_FAULT_DMA_ERROR);
    CHECK(a == AI_REL_RECOVERY_RETRY);
    CHECK(ai_rel_begin_recovery(&rel, d->id, a, 3ull));
    CHECK(d->health == AI_REL_HEALTH_RECOVERING);
    CHECK(d->retry_attempts == 1u);

    CHECK(ai_rel_recovery_succeeded(&rel, d->id, 4ull));
    CHECK(d->health == AI_REL_HEALTH_HEALTHY);
    CHECK(d->consecutive_failures == 0u);
    CHECK(d->generation == 2u);
    CHECK(d->total_recoveries == 1u);
    return 0;
}

static int test_recovery_escalation(void) {
    ai_reliability rel;
    ai_rel_policy p = ai_rel_default_policy();
    ai_rel_domain *d;
    ai_rel_recovery_action a;

    p.max_retry_attempts = 1u;
    p.quarantine_after_failures = 3u;
    p.fail_after_failures = 5u;
    ai_rel_init(&rel, &p, 0ull);
    d = ai_rel_domain_register(&rel, AI_REL_DOMAIN_REMOTE_NODE, 9ull, 1ull);
    CHECK(d != NULL);

    CHECK(ai_rel_record_fault(&rel, d->id, AI_REL_SEV_ERROR, AI_REL_FAULT_REMOTE_UNREACHABLE, 2ull));
    a = ai_rel_choose_recovery(&rel, d->id, AI_REL_FAULT_REMOTE_UNREACHABLE);
    CHECK(a == AI_REL_RECOVERY_RETRY);
    CHECK(ai_rel_begin_recovery(&rel, d->id, a, 3ull));

    a = ai_rel_choose_recovery(&rel, d->id, AI_REL_FAULT_REMOTE_UNREACHABLE);
    CHECK(a == AI_REL_RECOVERY_QUARANTINE);
    CHECK(ai_rel_begin_recovery(&rel, d->id, a, 4ull));
    CHECK(d->health == AI_REL_HEALTH_QUARANTINED);
    return 0;
}

static int test_heartbeat_detection(void) {
    ai_reliability rel;
    ai_rel_policy p = ai_rel_default_policy();
    ai_rel_domain *d;

    p.heartbeat_timeout_ns = 100ull;
    ai_rel_init(&rel, &p, 0ull);
    d = ai_rel_domain_register(&rel, AI_REL_DOMAIN_REMOTE_NODE, 99ull, 10ull);
    CHECK(d != NULL);

    CHECK(ai_rel_check_heartbeats(&rel, 100ull) == 0u);
    CHECK(ai_rel_check_heartbeats(&rel, 111ull) == 1u);
    CHECK(d->last_fault == AI_REL_FAULT_HEARTBEAT_LOST);
    CHECK(d->health == AI_REL_HEALTH_DEGRADED);
    CHECK(ai_rel_check_heartbeats(&rel, 200ull) == 0u);

    CHECK(ai_rel_heartbeat(&rel, d->id, 201ull));
    CHECK(d->last_heartbeat_ns == 201ull);
    return 0;
}

static int test_checkpoint_and_replay(void) {
    ai_reliability rel;
    ai_rel_checkpoint cp;
    u64 before;

    ai_rel_init(&rel, NULL, 0ull);
    ai_rel_journal_append(&rel, AI_REL_EVENT_SCHED_PICK, 1ull, 77ull, 2ull, 3ull, 1ull);
    ai_rel_journal_append(&rel, AI_REL_EVENT_RESOURCE_ASSIGN, 1ull, 77ull, 5ull, 0ull, 2ull);
    before = ai_rel_journal_checksum(&rel);
    cp = ai_rel_checkpoint_capture(&rel, 8ull, 4ull, 12ull, 9ull, 3ull);

    CHECK(cp.checkpoint_id == 1ull);
    CHECK(cp.graph_id == 8ull);
    CHECK(cp.graph_epoch == 4ull);
    CHECK(cp.scheduler_dispatch_count == 12ull);
    CHECK(cp.completed_work_count == 9ull);
    CHECK(cp.journal_checksum == before);
    CHECK(ai_rel_journal_count(&rel) == 4u);
    return 0;
}

static int test_ring_wrap(void) {
    ai_reliability rel;
    ai_rel_event oldest;
    u32 i;

    ai_rel_init(&rel, NULL, 0ull);
    for (i = 0u; i < AI_REL_JOURNAL_CAPACITY + 10u; ++i) {
        ai_rel_journal_append(
            &rel,
            AI_REL_EVENT_WORK_START,
            (u64)i,
            (u64)(1000u + i),
            0ull,
            0ull,
            (u64)i
        );
    }
    CHECK(ai_rel_journal_count(&rel) == AI_REL_JOURNAL_CAPACITY);
    CHECK(rel.journal.dropped_events == 11ull); /* BOOT + 266 appends -> 11 overwritten */
    CHECK(ai_rel_journal_get_oldest(&rel, 0u, &oldest));
    CHECK(oldest.sequence == 12ull);
    return 0;
}

static int test_fatal_failure(void) {
    ai_reliability rel;
    ai_rel_domain *d;

    ai_rel_init(&rel, NULL, 0ull);
    d = ai_rel_domain_register(&rel, AI_REL_DOMAIN_WORK_GRAPH, 55ull, 1ull);
    CHECK(d != NULL);
    CHECK(ai_rel_record_fault(
        &rel,
        d->id,
        AI_REL_SEV_FATAL,
        AI_REL_FAULT_INTERNAL_INVARIANT,
        2ull
    ));
    CHECK(d->health == AI_REL_HEALTH_FAILED);
    CHECK(ai_rel_safe_mode(&rel));
    CHECK(ai_rel_choose_recovery(&rel, d->id, d->last_fault) == AI_REL_RECOVERY_FAIL_DOMAIN);
    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_registration_and_journal();
    rc |= test_fault_transitions_and_safe_mode();
    rc |= test_recovery_policy();
    rc |= test_recovery_escalation();
    rc |= test_heartbeat_detection();
    rc |= test_checkpoint_and_replay();
    rc |= test_ring_wrap();
    rc |= test_fatal_failure();

    if (rc == 0) {
        puts("Phase 12 reliability tests: PASS");
    }
    return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
