#include <ai/policy.h>
#include <ai/policy_demo.h>
#include <kernel/printk.h>

static ai_policy_engine phase11_engine;

static void print_decision(const char *label, ai_policy_result r) {
    kputs(label);
    kputs(": ");
    kputs(r.allowed ? "ALLOW" : "DENY");
    kputs(" (");
    kputs(ai_policy_reason_name(r.reason));
    kputs(") grant=");
    kprint_u64(r.matched_grant_id);
    kputs("\n");
}

void ai_policy_run_phase11_demo(void) {
    kputs("\n[Phase 11: verified policy enforcement demo]\n");
    ai_policy_init(&phase11_engine);

    ai_policy_grant_spec tensor_spec;
    tensor_spec.subject_id = 42;
    tensor_spec.issuer_id = 1;
    tensor_spec.effect = AI_POLICY_EFFECT_ALLOW;
    tensor_spec.resource_kind = AI_RESOURCE_TENSOR;
    tensor_spec.resource_id = 100;
    tensor_spec.rights = AI_CAP_TENSOR_READ;
    tensor_spec.not_before_ns = 0;
    tensor_spec.expires_at_ns = 1000000;
    tensor_spec.byte_limit = 4096;
    tensor_spec.device_quota_bps = AI_POLICY_QUOTA_FULL_BPS;
    tensor_spec.max_delegation_depth = 2;
    ai_policy_grant *tensor_grant = ai_policy_add_grant(&phase11_engine, &tensor_spec);

    ai_policy_request req;
    req.subject_id = 42;
    req.resource_kind = AI_RESOURCE_TENSOR;
    req.resource_id = 100;
    req.rights = AI_CAP_TENSOR_READ;
    req.now_ns = 100;
    req.bytes = 1024;
    req.device_quota_bps = 0;
    print_decision("tensor 100 read", ai_policy_authorize(&phase11_engine, &req));

    req.rights = AI_CAP_TENSOR_WRITE;
    print_decision("tensor 100 write", ai_policy_authorize(&phase11_engine, &req));

    ai_policy_grant_spec gpu_spec;
    gpu_spec.subject_id = 42;
    gpu_spec.issuer_id = 1;
    gpu_spec.effect = AI_POLICY_EFFECT_ALLOW;
    gpu_spec.resource_kind = AI_RESOURCE_DEVICE;
    gpu_spec.resource_id = 0;
    gpu_spec.rights = AI_CAP_GPU_USE;
    gpu_spec.not_before_ns = 0;
    gpu_spec.expires_at_ns = 0;
    gpu_spec.byte_limit = 0;
    gpu_spec.device_quota_bps = 2000;
    gpu_spec.max_delegation_depth = 1;
    ai_policy_add_grant(&phase11_engine, &gpu_spec);

    req.subject_id = 42;
    req.resource_kind = AI_RESOURCE_DEVICE;
    req.resource_id = 0;
    req.rights = AI_CAP_GPU_USE;
    req.now_ns = 100;
    req.bytes = 0;
    req.device_quota_bps = 2500;
    print_decision("GPU0 at 25%", ai_policy_authorize(&phase11_engine, &req));

    req.device_quota_bps = 1500;
    print_decision("GPU0 at 15%", ai_policy_authorize(&phase11_engine, &req));

    if (tensor_grant) {
        ai_policy_delegation d;
        d.child_subject_id = 84;
        d.resource_kind = AI_RESOURCE_TENSOR;
        d.resource_id = 100;
        d.rights = AI_CAP_TENSOR_READ;
        d.not_before_ns = 100;
        d.expires_at_ns = 500000;
        d.byte_limit = 2048;
        d.device_quota_bps = AI_POLICY_QUOTA_FULL_BPS;
        ai_policy_result delegated = ai_policy_delegate(
            &phase11_engine, tensor_grant->id, 42, 100, &d);
        print_decision("delegate tensor read to agent 84", delegated);

        d.rights = AI_CAP_TENSOR_READ | AI_CAP_TENSOR_WRITE;
        print_decision("attempt privilege amplification",
                       ai_policy_delegate(&phase11_engine, tensor_grant->id, 42, 100, &d));

        ai_policy_revoke(&phase11_engine, tensor_grant->id, 1);
        req.subject_id = 84;
        req.resource_kind = AI_RESOURCE_TENSOR;
        req.resource_id = 100;
        req.rights = AI_CAP_TENSOR_READ;
        req.now_ns = 200;
        req.bytes = 512;
        req.device_quota_bps = 0;
        print_decision("delegated grant after parent revoke",
                       ai_policy_authorize(&phase11_engine, &req));
    }

    ai_policy_validation v = ai_policy_validate(&phase11_engine);
    kputs("policy invariants: ");
    kputs(v.valid ? "PASS" : "FAIL");
    kputs(" (");
    kputs(ai_policy_invariant_name(v.error));
    kputs(")\n");

    kputs("audit entries: ");
    kprint_u64(ai_policy_audit_count(&phase11_engine));
    kputs("\n");
}
