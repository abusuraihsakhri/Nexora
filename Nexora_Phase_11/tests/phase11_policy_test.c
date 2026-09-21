#include <stdio.h>
#include <stdlib.h>
#include <ai/policy.h>

static unsigned tests_run = 0;

#define CHECK(expr) do { \
    ++tests_run; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static ai_policy_grant *add_allow(
    ai_policy_engine *e,
    u64 subject,
    ai_resource_kind kind,
    u64 object,
    u64 rights,
    u64 start,
    u64 end,
    u64 bytes,
    u32 quota,
    u16 depth
) {
    ai_policy_grant_spec s;
    s.subject_id = subject;
    s.issuer_id = 1;
    s.effect = AI_POLICY_EFFECT_ALLOW;
    s.resource_kind = kind;
    s.resource_id = object;
    s.rights = rights;
    s.not_before_ns = start;
    s.expires_at_ns = end;
    s.byte_limit = bytes;
    s.device_quota_bps = quota;
    s.max_delegation_depth = depth;
    return ai_policy_add_grant(e, &s);
}

static ai_policy_result auth(
    ai_policy_engine *e,
    u64 subject,
    ai_resource_kind kind,
    u64 object,
    u64 rights,
    u64 now,
    u64 bytes,
    u32 quota
) {
    ai_policy_request r;
    r.subject_id = subject;
    r.resource_kind = kind;
    r.resource_id = object;
    r.rights = rights;
    r.now_ns = now;
    r.bytes = bytes;
    r.device_quota_bps = quota;
    return ai_policy_authorize(e, &r);
}

static void test_invalid_effect_and_wildcard_request(void) {
    ai_policy_engine e;
    ai_policy_init(&e);

    ai_policy_grant_spec s;
    s.subject_id = 10;
    s.issuer_id = 1;
    s.effect = (ai_policy_effect)99;
    s.resource_kind = AI_RESOURCE_TENSOR;
    s.resource_id = 1;
    s.rights = AI_CAP_TENSOR_READ;
    s.not_before_ns = 0;
    s.expires_at_ns = 0;
    s.byte_limit = 0;
    s.device_quota_bps = AI_POLICY_QUOTA_FULL_BPS;
    s.max_delegation_depth = 0;
    CHECK(ai_policy_add_grant(&e, &s) == NULL);

    ai_policy_grant *g = add_allow(&e, 10, AI_RESOURCE_TENSOR, AI_POLICY_ANY_ID,
                                    AI_CAP_TENSOR_READ, 0, 0, 0,
                                    AI_POLICY_QUOTA_FULL_BPS, 0);
    CHECK(g != NULL);
    ai_policy_result r = auth(&e, 10, AI_RESOURCE_TENSOR, AI_POLICY_ANY_ID,
                              AI_CAP_TENSOR_READ, 1, 1, 0);
    CHECK(!r.allowed);
    CHECK(r.reason == AI_POLICY_REASON_DENY_INVALID_REQUEST);

    g->effect = (ai_policy_effect)99;
    ai_policy_validation v = ai_policy_validate(&e);
    CHECK(!v.valid);
    CHECK(v.error == AI_POLICY_INVARIANT_BAD_EFFECT);
    g->effect = AI_POLICY_EFFECT_ALLOW;
    CHECK(ai_policy_validate(&e).valid);
}

static void test_table_reclamation(void) {
    ai_policy_engine e;
    ai_policy_init(&e);

    u64 first_id = 0;
    for (u32 i = 0; i < AI_POLICY_MAX_GRANTS; ++i) {
        ai_policy_grant *g = add_allow(&e, 100 + i, AI_RESOURCE_MODEL, i,
                                       AI_CAP_MODEL_USE, 0, 0, 0,
                                       AI_POLICY_QUOTA_FULL_BPS, 0);
        CHECK(g != NULL);
        if (i == 0) first_id = g->id;
    }
    CHECK(e.grant_count == AI_POLICY_MAX_GRANTS);
    CHECK(add_allow(&e, 9999, AI_RESOURCE_MODEL, 9999, AI_CAP_MODEL_USE,
                    0, 0, 0, AI_POLICY_QUOTA_FULL_BPS, 0) == NULL);

    /* Revoked slots are reclaimed automatically when the table is full. */
    CHECK(ai_policy_revoke(&e, first_id, 1));
    ai_policy_grant *replacement = add_allow(&e, 9999, AI_RESOURCE_MODEL, 9999,
                                              AI_CAP_MODEL_USE, 0, 0, 0,
                                              AI_POLICY_QUOTA_FULL_BPS, 0);
    CHECK(replacement != NULL);
    CHECK(e.grant_count == AI_POLICY_MAX_GRANTS);
    CHECK(ai_policy_find_grant(&e, first_id) == NULL);
    CHECK(ai_policy_validate(&e).valid);

    /* Epoch invalidation immediately releases every stale slot. */
    u64 old_epoch = e.epoch;
    CHECK(ai_policy_bump_epoch(&e, 1) == old_epoch + 1);
    CHECK(e.grant_count == 0);
    CHECK(add_allow(&e, 7, AI_RESOURCE_MODEL, 7, AI_CAP_MODEL_USE,
                    0, 0, 0, AI_POLICY_QUOTA_FULL_BPS, 0) != NULL);
}

int main(void) {
    ai_policy_engine e;
    ai_policy_init(&e);

    /* 1. Deny by default. */
    ai_policy_result r = auth(&e, 10, AI_RESOURCE_TENSOR, 1,
                              AI_CAP_TENSOR_READ, 10, 64, 0);
    CHECK(!r.allowed);
    CHECK(r.reason == AI_POLICY_REASON_DENY_NO_MATCH);

    /* 2. Exact allow and rights isolation. */
    ai_policy_grant *tensor = add_allow(&e, 10, AI_RESOURCE_TENSOR, 1,
                                        AI_CAP_TENSOR_READ, 0, 1000,
                                        1024, AI_POLICY_QUOTA_FULL_BPS, 2);
    CHECK(tensor != NULL);
    u64 tensor_id = tensor->id;
    CHECK(auth(&e, 10, AI_RESOURCE_TENSOR, 1,
               AI_CAP_TENSOR_READ, 10, 512, 0).allowed);
    CHECK(!auth(&e, 10, AI_RESOURCE_TENSOR, 1,
                AI_CAP_TENSOR_WRITE, 10, 512, 0).allowed);
    CHECK(!auth(&e, 11, AI_RESOURCE_TENSOR, 1,
                AI_CAP_TENSOR_READ, 10, 512, 0).allowed);
    CHECK(!auth(&e, 10, AI_RESOURCE_TENSOR, 2,
                AI_CAP_TENSOR_READ, 10, 512, 0).allowed);

    /* 3. Time and byte constraints. byte_limit is per request, not cumulative. */
    CHECK(!auth(&e, 10, AI_RESOURCE_TENSOR, 1,
                AI_CAP_TENSOR_READ, 1000, 512, 0).allowed);
    CHECK(!auth(&e, 10, AI_RESOURCE_TENSOR, 1,
                AI_CAP_TENSOR_READ, 10, 1025, 0).allowed);
    CHECK(auth(&e, 10, AI_RESOURCE_TENSOR, 1,
               AI_CAP_TENSOR_READ, 10, 1024, 0).allowed);
    CHECK(auth(&e, 10, AI_RESOURCE_TENSOR, 1,
               AI_CAP_TENSOR_READ, 11, 1024, 0).allowed);

    /* 4. Device quota compares trusted per-operation demand, in basis points. */
    ai_policy_grant *gpu = add_allow(&e, 10, AI_RESOURCE_DEVICE, 0,
                                     AI_CAP_GPU_USE, 0, 0, 0, 2000, 1);
    CHECK(gpu != NULL);
    CHECK(auth(&e, 10, AI_RESOURCE_DEVICE, 0,
               AI_CAP_GPU_USE, 10, 0, 1500).allowed);
    r = auth(&e, 10, AI_RESOURCE_DEVICE, 0,
             AI_CAP_GPU_USE, 10, 0, 2500);
    CHECK(!r.allowed);
    CHECK(r.reason == AI_POLICY_REASON_DENY_QUOTA);

    /* 5. Explicit deny overrides allow. */
    ai_policy_grant *network_allow = add_allow(&e, 10, AI_RESOURCE_NETWORK_ENDPOINT,
                                               AI_POLICY_ANY_ID, AI_CAP_NETWORK,
                                               0, 0, 0, AI_POLICY_QUOTA_FULL_BPS, 0);
    CHECK(network_allow != NULL);
    ai_policy_grant_spec deny;
    deny.subject_id = 10;
    deny.issuer_id = 1;
    deny.effect = AI_POLICY_EFFECT_DENY;
    deny.resource_kind = AI_RESOURCE_NETWORK_ENDPOINT;
    deny.resource_id = 7;
    deny.rights = AI_CAP_NETWORK;
    deny.not_before_ns = 0;
    deny.expires_at_ns = 0;
    deny.byte_limit = 0;
    deny.device_quota_bps = AI_POLICY_QUOTA_FULL_BPS;
    deny.max_delegation_depth = 0;
    CHECK(ai_policy_add_grant(&e, &deny) != NULL);
    CHECK(auth(&e, 10, AI_RESOURCE_NETWORK_ENDPOINT, 8,
               AI_CAP_NETWORK, 10, 0, 0).allowed);
    r = auth(&e, 10, AI_RESOURCE_NETWORK_ENDPOINT, 7,
             AI_CAP_NETWORK, 10, 0, 0);
    CHECK(!r.allowed);
    CHECK(r.reason == AI_POLICY_REASON_DENY_EXPLICIT);

    /* 6. Delegation must attenuate rights, object, time, bytes and quota. */
    ai_policy_delegation d;
    d.child_subject_id = 20;
    d.resource_kind = AI_RESOURCE_TENSOR;
    d.resource_id = 1;
    d.rights = AI_CAP_TENSOR_READ;
    d.not_before_ns = 20;
    d.expires_at_ns = 500;
    d.byte_limit = 512;
    d.device_quota_bps = AI_POLICY_QUOTA_FULL_BPS;
    ai_policy_result dr = ai_policy_delegate(&e, tensor_id, 10, 20, &d);
    CHECK(dr.allowed);
    CHECK(dr.matched_grant_id != 0);
    CHECK(auth(&e, 20, AI_RESOURCE_TENSOR, 1,
               AI_CAP_TENSOR_READ, 30, 256, 0).allowed);

    d.rights = AI_CAP_TENSOR_READ | AI_CAP_TENSOR_WRITE;
    CHECK(!ai_policy_delegate(&e, tensor_id, 10, 20, &d).allowed);
    d.rights = AI_CAP_TENSOR_READ;
    d.resource_id = AI_POLICY_ANY_ID;
    CHECK(!ai_policy_delegate(&e, tensor_id, 10, 20, &d).allowed);
    d.resource_id = 1;
    d.expires_at_ns = 2000;
    CHECK(!ai_policy_delegate(&e, tensor_id, 10, 20, &d).allowed);
    d.expires_at_ns = 500;
    d.byte_limit = 2048;
    CHECK(!ai_policy_delegate(&e, tensor_id, 10, 20, &d).allowed);

    /* 7. Revocation cascades to descendants; explicit reclaim removes stale slots. */
    CHECK(ai_policy_revoke(&e, tensor_id, 1));
    r = auth(&e, 20, AI_RESOURCE_TENSOR, 1,
             AI_CAP_TENSOR_READ, 30, 128, 0);
    CHECK(!r.allowed);
    CHECK(r.reason == AI_POLICY_REASON_DENY_REVOKED);
    u32 before_reclaim = e.grant_count;
    CHECK(ai_policy_reclaim_inactive(&e) >= 2);
    CHECK(e.grant_count < before_reclaim);
    CHECK(ai_policy_find_grant(&e, tensor_id) == NULL);

    /* 8. Epoch bump invalidates and reclaims all pre-bump grants. */
    CHECK(auth(&e, 10, AI_RESOURCE_DEVICE, 0,
               AI_CAP_GPU_USE, 10, 0, 1000).allowed);
    u64 old_epoch = e.epoch;
    CHECK(ai_policy_bump_epoch(&e, 1) == old_epoch + 1);
    CHECK(e.grant_count == 0);
    r = auth(&e, 10, AI_RESOURCE_DEVICE, 0,
             AI_CAP_GPU_USE, 10, 0, 1000);
    CHECK(!r.allowed);
    CHECK(r.reason == AI_POLICY_REASON_DENY_NO_MATCH);

    /* 9. New epoch grants work; invariants stay valid. */
    CHECK(add_allow(&e, 30, AI_RESOURCE_MODEL, 9,
                    AI_CAP_MODEL_USE | AI_CAP_EXECUTE,
                    0, 0, 0, AI_POLICY_QUOTA_FULL_BPS, 1) != NULL);
    CHECK(auth(&e, 30, AI_RESOURCE_MODEL, 9,
               AI_CAP_MODEL_USE, 1, 0, 0).allowed);
    ai_policy_validation v = ai_policy_validate(&e);
    CHECK(v.valid);
    CHECK(v.error == AI_POLICY_INVARIANT_OK);

    /* 10. Audit is ordered and nonempty. */
    u32 n = ai_policy_audit_count(&e);
    CHECK(n >= 10);
    ai_policy_audit_entry first;
    ai_policy_audit_entry last;
    CHECK(ai_policy_audit_get(&e, 0, &first));
    CHECK(ai_policy_audit_get(&e, n - 1u, &last));
    CHECK(last.sequence >= first.sequence);

    test_invalid_effect_and_wildcard_request();
    test_table_reclamation();

    printf("Phase 11.1 policy tests: PASS (%u checks, %u audit entries in primary engine)\n",
           tests_run, n);
    return 0;
}
