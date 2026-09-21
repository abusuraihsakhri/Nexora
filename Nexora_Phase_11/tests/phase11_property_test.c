#include <stdio.h>
#include <stdlib.h>
#include <ai/policy.h>

static u64 rng_state = 0x9E3779B97F4A7C15ull;

static u64 next_rand(void) {
    u64 x = rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_state = x;
    return x * 2685821657736338717ull;
}

static void fail(unsigned iter, const char *msg) {
    fprintf(stderr, "property FAIL at iteration %u: %s\n", iter, msg);
    exit(1);
}

int main(void) {
    const u64 now = 100;

    for (unsigned i = 0; i < 5000; ++i) {
        ai_policy_engine e;
        ai_policy_init(&e);

        ai_policy_grant_spec p;
        p.subject_id = 100;
        p.issuer_id = 1;
        p.effect = AI_POLICY_EFFECT_ALLOW;
        p.resource_kind = AI_RESOURCE_TENSOR;
        p.resource_id = 55;
        p.rights = AI_CAP_TENSOR_READ | AI_CAP_TENSOR_WRITE;
        p.not_before_ns = 0;
        p.expires_at_ns = 10000;
        p.byte_limit = 4096;
        p.device_quota_bps = AI_POLICY_QUOTA_FULL_BPS;
        p.max_delegation_depth = 2;
        ai_policy_grant *parent = ai_policy_add_grant(&e, &p);
        if (!parent) fail(i, "parent grant creation");

        u64 rights_choices[] = {
            AI_CAP_TENSOR_READ,
            AI_CAP_TENSOR_WRITE,
            AI_CAP_TENSOR_READ | AI_CAP_TENSOR_WRITE,
            AI_CAP_TENSOR_READ | AI_CAP_GPU_USE,
            AI_CAP_GPU_USE
        };

        ai_policy_delegation d;
        d.child_subject_id = 200 + (next_rand() % 7);
        d.resource_kind = (next_rand() & 1u) ? AI_RESOURCE_TENSOR : AI_RESOURCE_MODEL;
        d.resource_id = (next_rand() & 1u) ? 55 : AI_POLICY_ANY_ID;
        d.rights = rights_choices[next_rand() % 5];
        d.not_before_ns = next_rand() % 250;
        d.expires_at_ns = 50 + (next_rand() % 12000);
        d.byte_limit = next_rand() % 6000;
        d.device_quota_bps = 1 + (u32)(next_rand() % 12000);

        bool expected = true;
        expected = expected && ((d.rights & p.rights) == d.rights);
        expected = expected && d.resource_kind == AI_RESOURCE_TENSOR;
        expected = expected && d.resource_id == 55;
        expected = expected && d.not_before_ns >= now;
        expected = expected && d.expires_at_ns > d.not_before_ns;
        expected = expected && d.expires_at_ns <= p.expires_at_ns;
        expected = expected && d.byte_limit > 0 && d.byte_limit <= p.byte_limit;
        expected = expected && d.device_quota_bps <= p.device_quota_bps;

        ai_policy_result r = ai_policy_delegate(&e, parent->id, 100, now, &d);
        if (r.allowed != expected) fail(i, "delegation acceptance differs from attenuation model");

        if (r.allowed) {
            const ai_policy_grant *child = ai_policy_find_grant(&e, r.matched_grant_id);
            if (!child) fail(i, "accepted child missing");
            if ((child->rights & parent->rights) != child->rights) fail(i, "rights amplification");
            if (child->resource_kind != AI_RESOURCE_TENSOR || child->resource_id != 55) fail(i, "resource widening");
            if (child->not_before_ns < now || child->expires_at_ns > parent->expires_at_ns) fail(i, "time widening");
            if (child->byte_limit == 0 || child->byte_limit > parent->byte_limit) fail(i, "byte widening");
            if (child->device_quota_bps > parent->device_quota_bps) fail(i, "quota widening");
            ai_policy_validation v = ai_policy_validate(&e);
            if (!v.valid) fail(i, "invariant validator rejected accepted state");
        }
    }

    /* Force the audit ring to wrap and verify logical order remains monotonic. */
    ai_policy_engine e;
    ai_policy_init(&e);
    for (unsigned i = 0; i < 400; ++i) {
        ai_policy_request r;
        r.subject_id = 999;
        r.resource_kind = AI_RESOURCE_TENSOR;
        r.resource_id = i;
        r.rights = AI_CAP_TENSOR_READ;
        r.now_ns = i;
        r.bytes = 1;
        r.device_quota_bps = 0;
        (void)ai_policy_authorize(&e, &r);
    }

    if (ai_policy_audit_count(&e) != AI_POLICY_AUDIT_CAPACITY) {
        fail(5000, "audit ring count after wrap");
    }

    ai_policy_audit_entry prev;
    ai_policy_audit_entry cur;
    if (!ai_policy_audit_get(&e, 0, &prev)) fail(5000, "first audit read");
    for (u32 i = 1; i < ai_policy_audit_count(&e); ++i) {
        if (!ai_policy_audit_get(&e, i, &cur)) fail(5000, "audit read");
        if (cur.sequence != prev.sequence + 1u) fail(5000, "audit order after wrap");
        prev = cur;
    }

    printf("Phase 11.1 property tests: PASS (5000 attenuation trials + audit wrap)\n");
    return 0;
}
