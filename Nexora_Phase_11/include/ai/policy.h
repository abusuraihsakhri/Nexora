#ifndef NEXORA_AI_POLICY_H
#define NEXORA_AI_POLICY_H

#include <kernel/types.h>
#include <ai/capability.h>

#define AI_POLICY_MAX_GRANTS       128u
#define AI_POLICY_AUDIT_CAPACITY   256u
#define AI_POLICY_ANY_ID           (~0ull)
#define AI_POLICY_QUOTA_FULL_BPS   10000u

typedef enum {
    AI_RESOURCE_ANY = 0,
    AI_RESOURCE_TENSOR,
    AI_RESOURCE_MODEL,
    AI_RESOURCE_DEVICE,
    AI_RESOURCE_NETWORK_ENDPOINT,
    AI_RESOURCE_REMOTE_TENSOR,
    AI_RESOURCE_COLLECTIVE
} ai_resource_kind;

typedef enum {
    AI_POLICY_EFFECT_ALLOW = 0,
    AI_POLICY_EFFECT_DENY
} ai_policy_effect;

typedef enum {
    AI_POLICY_REASON_ALLOWED = 0,
    AI_POLICY_REASON_DENY_EXPLICIT,
    AI_POLICY_REASON_DENY_NO_MATCH,
    AI_POLICY_REASON_DENY_RIGHTS,
    AI_POLICY_REASON_DENY_NOT_YET_VALID,
    AI_POLICY_REASON_DENY_EXPIRED,
    AI_POLICY_REASON_DENY_BYTE_LIMIT,
    AI_POLICY_REASON_DENY_QUOTA,
    AI_POLICY_REASON_DENY_REVOKED,
    AI_POLICY_REASON_DENY_EPOCH,
    AI_POLICY_REASON_DENY_DELEGATION,
    AI_POLICY_REASON_DENY_INVALID_REQUEST,
    AI_POLICY_REASON_DENY_TABLE_FULL
} ai_policy_reason;

typedef enum {
    AI_AUDIT_AUTHORIZE = 0,
    AI_AUDIT_DELEGATE,
    AI_AUDIT_REVOKE,
    AI_AUDIT_EPOCH_BUMP
} ai_audit_action;

typedef enum {
    AI_POLICY_INVARIANT_OK = 0,
    AI_POLICY_INVARIANT_BAD_ID,
    AI_POLICY_INVARIANT_DUPLICATE_ID,
    AI_POLICY_INVARIANT_BAD_EFFECT,
    AI_POLICY_INVARIANT_ZERO_RIGHTS,
    AI_POLICY_INVARIANT_BAD_RESOURCE,
    AI_POLICY_INVARIANT_BAD_TIME_WINDOW,
    AI_POLICY_INVARIANT_BAD_QUOTA,
    AI_POLICY_INVARIANT_BAD_DEPTH,
    AI_POLICY_INVARIANT_PARENT_MISSING,
    AI_POLICY_INVARIANT_PARENT_NOT_ALLOW,
    AI_POLICY_INVARIANT_PARENT_INACTIVE,
    AI_POLICY_INVARIANT_ISSUER_MISMATCH,
    AI_POLICY_INVARIANT_RIGHTS_AMPLIFIED,
    AI_POLICY_INVARIANT_RESOURCE_WIDENED,
    AI_POLICY_INVARIANT_TIME_WIDENED,
    AI_POLICY_INVARIANT_BYTES_WIDENED,
    AI_POLICY_INVARIANT_QUOTA_WIDENED,
    AI_POLICY_INVARIANT_EPOCH_MISMATCH
} ai_policy_invariant_error;

typedef struct {
    u64 subject_id;
    u64 issuer_id;
    ai_policy_effect effect;
    ai_resource_kind resource_kind;
    u64 resource_id;
    u64 rights;
    u64 not_before_ns;
    u64 expires_at_ns;
    /* Per-request byte ceiling. Zero means unbounded; this is not cumulative accounting. */
    u64 byte_limit;
    /* Per-request accelerator demand ceiling in basis points. */
    u32 device_quota_bps;
    u16 max_delegation_depth;
} ai_policy_grant_spec;

typedef struct {
    u64 id;
    u64 parent_id;
    u64 subject_id;
    u64 issuer_id;
    ai_policy_effect effect;
    ai_resource_kind resource_kind;
    u64 resource_id;
    u64 rights;
    u64 not_before_ns;
    u64 expires_at_ns;
    u64 byte_limit;
    u32 device_quota_bps;
    u16 delegation_depth;
    u16 max_delegation_depth;
    u64 epoch;
    bool active;
} ai_policy_grant;

typedef struct {
    u64 subject_id;
    ai_resource_kind resource_kind;
    /* AI_POLICY_ANY_ID is reserved for policy wildcards and is invalid in a request. */
    u64 resource_id;
    u64 rights;
    u64 now_ns;
    /* Bytes demanded by this operation, not accumulated lifetime consumption. */
    u64 bytes;
    /* Accelerator share demanded by this operation; callers must provide trusted accounting. */
    u32 device_quota_bps;
} ai_policy_request;

typedef struct {
    u64 child_subject_id;
    ai_resource_kind resource_kind;
    u64 resource_id;
    u64 rights;
    u64 not_before_ns;
    u64 expires_at_ns;
    u64 byte_limit;
    u32 device_quota_bps;
} ai_policy_delegation;

typedef struct {
    bool allowed;
    ai_policy_reason reason;
    u64 matched_grant_id;
} ai_policy_result;

typedef struct {
    bool valid;
    ai_policy_invariant_error error;
    u64 grant_id;
} ai_policy_validation;

typedef struct {
    u64 sequence;
    ai_audit_action action;
    u64 subject_id;
    ai_resource_kind resource_kind;
    u64 resource_id;
    u64 rights;
    u64 matched_grant_id;
    u64 epoch;
    bool allowed;
    ai_policy_reason reason;
} ai_policy_audit_entry;

typedef struct {
    ai_policy_grant grants[AI_POLICY_MAX_GRANTS];
    u32 grant_count;
    u64 next_grant_id;
    u64 epoch;

    ai_policy_audit_entry audit[AI_POLICY_AUDIT_CAPACITY];
    u32 audit_next;
    u32 audit_count;
    u64 audit_sequence;
} ai_policy_engine;

/*
 * Synchronization contract
 * ------------------------
 * ai_policy_engine is deliberately lock-free and contains no internal SMP
 * synchronization. Kernel integration must serialize access externally.
 * A privileged operation must not race authorization against revoke/epoch
 * mutation; hold the policy/resource lock across authorization and resource
 * commitment, or revalidate before committing the privileged operation.
 */
void ai_policy_init(ai_policy_engine *engine);

ai_policy_grant *ai_policy_add_grant(
    ai_policy_engine *engine,
    const ai_policy_grant_spec *spec
);

ai_policy_result ai_policy_authorize(
    ai_policy_engine *engine,
    const ai_policy_request *request
);

ai_policy_result ai_policy_delegate(
    ai_policy_engine *engine,
    u64 parent_grant_id,
    u64 delegator_subject_id,
    u64 now_ns,
    const ai_policy_delegation *delegation
);

/*
 * Management-operation trust contract
 * -----------------------------------
 * ai_policy_revoke() and ai_policy_bump_epoch() are kernel-management
 * primitives. actor_subject_id is audit attribution, not an authorization
 * check. Do not expose these functions directly to untrusted callers; a
 * syscall/control-plane wrapper must authenticate and authorize management
 * authority before calling them.
 */
bool ai_policy_revoke(ai_policy_engine *engine, u64 grant_id, u64 actor_subject_id);
u64 ai_policy_bump_epoch(ai_policy_engine *engine, u64 actor_subject_id);

/* Remove inactive grants while preserving IDs and parent links of active grants. */
u32 ai_policy_reclaim_inactive(ai_policy_engine *engine);

const ai_policy_grant *ai_policy_find_grant(const ai_policy_engine *engine, u64 grant_id);
ai_policy_validation ai_policy_validate(const ai_policy_engine *engine);

u32 ai_policy_audit_count(const ai_policy_engine *engine);
bool ai_policy_audit_get(
    const ai_policy_engine *engine,
    u32 logical_index,
    ai_policy_audit_entry *out
);

const char *ai_policy_reason_name(ai_policy_reason reason);
const char *ai_resource_kind_name(ai_resource_kind kind);
const char *ai_policy_effect_name(ai_policy_effect effect);
const char *ai_audit_action_name(ai_audit_action action);
const char *ai_policy_invariant_name(ai_policy_invariant_error error);

#endif
