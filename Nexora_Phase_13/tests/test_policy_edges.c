#include <stdio.h>
#include <stdlib.h>
#include <ai/reliability.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL:%s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return EXIT_FAILURE; \
    } \
} while (0)

int main(void) {
    ai_reliability rel;
    ai_rel_policy p;
    ai_rel_domain *d;

    p.degrade_after_failures = 0u;
    p.quarantine_after_failures = 0u;
    p.fail_after_failures = 0u;
    p.max_retry_attempts = 0u;
    p.quarantine_ns = 0ull;
    p.heartbeat_timeout_ns = 0ull;
    p.safe_mode_failed_domains = 0u;

    ai_rel_init(&rel, &p, 0ull);
    CHECK(rel.policy.degrade_after_failures == 1u);
    CHECK(rel.policy.quarantine_after_failures == 1u);
    CHECK(rel.policy.fail_after_failures == 1u);
    CHECK(rel.policy.max_retry_attempts == 1u);
    CHECK(rel.policy.quarantine_ns > 0ull);
    CHECK(rel.policy.heartbeat_timeout_ns > 0ull);
    CHECK(rel.policy.safe_mode_failed_domains == 1u);

    d = ai_rel_domain_register(&rel, AI_REL_DOMAIN_AGENT, 1ull, 1ull);
    CHECK(d != NULL);
    CHECK(ai_rel_record_fault(&rel, d->id, AI_REL_SEV_ERROR, AI_REL_FAULT_WORK_FAILED, 2ull));
    CHECK(d->health == AI_REL_HEALTH_FAILED);

    puts("Phase 12 policy edge tests: PASS");
    return EXIT_SUCCESS;
}
