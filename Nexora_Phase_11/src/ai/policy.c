#include <ai/policy.h>

static bool valid_resource_kind(ai_resource_kind kind) {
    return kind >= AI_RESOURCE_ANY && kind <= AI_RESOURCE_COLLECTIVE;
}

static bool valid_effect(ai_policy_effect effect) {
    return effect == AI_POLICY_EFFECT_ALLOW || effect == AI_POLICY_EFFECT_DENY;
}

static bool resource_spec_valid(ai_resource_kind kind, u64 resource_id) {
    if (!valid_resource_kind(kind)) return false;
    if (kind == AI_RESOURCE_ANY && resource_id != AI_POLICY_ANY_ID) return false;
    return true;
}

static bool request_resource_valid(ai_resource_kind kind, u64 resource_id) {
    if (!valid_resource_kind(kind) || kind == AI_RESOURCE_ANY) return false;
    /* AI_POLICY_ANY_ID is a policy wildcard sentinel, never a concrete object ID. */
    return resource_id != AI_POLICY_ANY_ID;
}

static bool resource_matches(
    ai_resource_kind grant_kind,
    u64 grant_id,
    ai_resource_kind request_kind,
    u64 request_id
) {
    if (grant_kind != AI_RESOURCE_ANY && grant_kind != request_kind) return false;
    if (grant_id != AI_POLICY_ANY_ID && grant_id != request_id) return false;
    return true;
}

static bool resource_narrows(
    ai_resource_kind parent_kind,
    u64 parent_id,
    ai_resource_kind child_kind,
    u64 child_id
) {
    if (!resource_spec_valid(parent_kind, parent_id) ||
        !resource_spec_valid(child_kind, child_id)) {
        return false;
    }

    if (parent_kind != AI_RESOURCE_ANY && child_kind != parent_kind) return false;
    if (parent_kind == AI_RESOURCE_ANY && child_kind == AI_RESOURCE_ANY &&
        child_id != AI_POLICY_ANY_ID) {
        return false;
    }

    if (parent_id != AI_POLICY_ANY_ID && child_id != parent_id) return false;
    if (parent_id != AI_POLICY_ANY_ID && child_id == AI_POLICY_ANY_ID) return false;
    return true;
}

static bool time_active(const ai_policy_grant *grant, u64 now_ns) {
    if (now_ns < grant->not_before_ns) return false;
    if (grant->expires_at_ns != 0 && now_ns >= grant->expires_at_ns) return false;
    return true;
}

static bool time_narrows(
    const ai_policy_grant *parent,
    u64 now_ns,
    u64 child_not_before_ns,
    u64 child_expires_at_ns
) {
    if (child_not_before_ns < now_ns) return false;
    if (child_not_before_ns < parent->not_before_ns) return false;
    if (child_expires_at_ns != 0 && child_expires_at_ns <= child_not_before_ns) return false;

    if (parent->expires_at_ns != 0) {
        if (child_expires_at_ns == 0) return false;
        if (child_expires_at_ns > parent->expires_at_ns) return false;
    }
    return true;
}

static bool bytes_narrow(u64 parent_limit, u64 child_limit) {
    if (parent_limit == 0) return true;
    if (child_limit == 0) return false;
    return child_limit <= parent_limit;
}

static void audit_record(
    ai_policy_engine *engine,
    ai_audit_action action,
    u64 subject_id,
    ai_resource_kind kind,
    u64 resource_id,
    u64 rights,
    bool allowed,
    ai_policy_reason reason,
    u64 matched_grant_id
) {
    if (!engine) return;

    ai_policy_audit_entry *entry = &engine->audit[engine->audit_next];
    entry->sequence = ++engine->audit_sequence;
    entry->action = action;
    entry->subject_id = subject_id;
    entry->resource_kind = kind;
    entry->resource_id = resource_id;
    entry->rights = rights;
    entry->matched_grant_id = matched_grant_id;
    entry->epoch = engine->epoch;
    entry->allowed = allowed;
    entry->reason = reason;

    engine->audit_next = (engine->audit_next + 1u) % AI_POLICY_AUDIT_CAPACITY;
    if (engine->audit_count < AI_POLICY_AUDIT_CAPACITY) {
        engine->audit_count++;
    }
}

static ai_policy_grant *find_grant_mut(ai_policy_engine *engine, u64 grant_id) {
    if (!engine || grant_id == 0) return NULL;
    for (u32 i = 0; i < engine->grant_count; ++i) {
        if (engine->grants[i].id == grant_id) return &engine->grants[i];
    }
    return NULL;
}

const ai_policy_grant *ai_policy_find_grant(const ai_policy_engine *engine, u64 grant_id) {
    if (!engine || grant_id == 0) return NULL;
    for (u32 i = 0; i < engine->grant_count; ++i) {
        if (engine->grants[i].id == grant_id) return &engine->grants[i];
    }
    return NULL;
}

u32 ai_policy_reclaim_inactive(ai_policy_engine *engine) {
    if (!engine) return 0;

    u32 removed = 0;
    for (u32 i = 0; i < engine->grant_count; ++i) {
        ai_policy_grant *g = &engine->grants[i];
        if (g->id == 0 || g->active) continue;
        /* Mark a reusable hole without relocating any active grant. */
        g->id = 0;
        g->parent_id = 0;
        g->active = false;
        removed++;
    }

    while (engine->grant_count > 0 &&
           engine->grants[engine->grant_count - 1u].id == 0) {
        engine->grant_count--;
    }
    return removed;
}

static ai_policy_grant *allocate_grant_slot(ai_policy_engine *engine) {
    if (!engine) return NULL;

    for (u32 i = 0; i < engine->grant_count; ++i) {
        if (engine->grants[i].id == 0) return &engine->grants[i];
    }

    if (engine->grant_count < AI_POLICY_MAX_GRANTS) {
        return &engine->grants[engine->grant_count++];
    }

    (void)ai_policy_reclaim_inactive(engine);
    for (u32 i = 0; i < engine->grant_count; ++i) {
        if (engine->grants[i].id == 0) return &engine->grants[i];
    }

    if (engine->grant_count < AI_POLICY_MAX_GRANTS) {
        return &engine->grants[engine->grant_count++];
    }
    return NULL;
}

void ai_policy_init(ai_policy_engine *engine) {
    if (!engine) return;
    engine->grant_count = 0;
    engine->next_grant_id = 1;
    engine->epoch = 1;
    engine->audit_next = 0;
    engine->audit_count = 0;
    engine->audit_sequence = 0;
}

ai_policy_grant *ai_policy_add_grant(
    ai_policy_engine *engine,
    const ai_policy_grant_spec *spec
) {
    if (!engine || !spec) return NULL;
    if (!valid_effect(spec->effect)) return NULL;
    if (spec->rights == 0) return NULL;
    if (!resource_spec_valid(spec->resource_kind, spec->resource_id)) return NULL;
    if (spec->expires_at_ns != 0 && spec->expires_at_ns <= spec->not_before_ns) return NULL;
    if (spec->device_quota_bps == 0 || spec->device_quota_bps > AI_POLICY_QUOTA_FULL_BPS) return NULL;

    ai_policy_grant *grant = allocate_grant_slot(engine);
    if (!grant) return NULL;
    grant->id = engine->next_grant_id++;
    grant->parent_id = 0;
    grant->subject_id = spec->subject_id;
    grant->issuer_id = spec->issuer_id;
    grant->effect = spec->effect;
    grant->resource_kind = spec->resource_kind;
    grant->resource_id = spec->resource_id;
    grant->rights = spec->rights;
    grant->not_before_ns = spec->not_before_ns;
    grant->expires_at_ns = spec->expires_at_ns;
    grant->byte_limit = spec->byte_limit;
    grant->device_quota_bps = spec->device_quota_bps;
    grant->delegation_depth = 0;
    grant->max_delegation_depth = spec->max_delegation_depth;
    grant->epoch = engine->epoch;
    grant->active = true;
    return grant;
}

static ai_policy_result result(bool allowed, ai_policy_reason reason, u64 grant_id) {
    ai_policy_result r;
    r.allowed = allowed;
    r.reason = reason;
    r.matched_grant_id = grant_id;
    return r;
}

ai_policy_result ai_policy_authorize(
    ai_policy_engine *engine,
    const ai_policy_request *request
) {
    if (!engine || !request || request->rights == 0 ||
        !request_resource_valid(request->resource_kind, request->resource_id) ||
        request->device_quota_bps > AI_POLICY_QUOTA_FULL_BPS) {
        ai_policy_result r = result(false, AI_POLICY_REASON_DENY_INVALID_REQUEST, 0);
        if (engine && request) {
            audit_record(engine, AI_AUDIT_AUTHORIZE, request->subject_id,
                         request->resource_kind, request->resource_id,
                         request->rights, false, r.reason, 0);
        }
        return r;
    }

    bool saw_resource = false;
    bool saw_revoked = false;
    bool saw_epoch = false;
    bool saw_not_yet = false;
    bool saw_expired = false;
    bool saw_rights = false;
    bool saw_bytes = false;
    bool saw_quota = false;

    /* Explicit deny has precedence over any allow. */
    for (u32 i = 0; i < engine->grant_count; ++i) {
        const ai_policy_grant *g = &engine->grants[i];
        if (g->id == 0) continue;
        if (g->subject_id != request->subject_id) continue;
        if (!resource_matches(g->resource_kind, g->resource_id,
                              request->resource_kind, request->resource_id)) continue;
        if (g->effect != AI_POLICY_EFFECT_DENY) continue;
        if (!g->active || g->epoch != engine->epoch || !time_active(g, request->now_ns)) continue;
        if ((g->rights & request->rights) == 0) continue;

        ai_policy_result r = result(false, AI_POLICY_REASON_DENY_EXPLICIT, g->id);
        audit_record(engine, AI_AUDIT_AUTHORIZE, request->subject_id,
                     request->resource_kind, request->resource_id,
                     request->rights, false, r.reason, g->id);
        return r;
    }

    for (u32 i = 0; i < engine->grant_count; ++i) {
        const ai_policy_grant *g = &engine->grants[i];
        if (g->id == 0) continue;
        if (g->subject_id != request->subject_id) continue;
        if (!resource_matches(g->resource_kind, g->resource_id,
                              request->resource_kind, request->resource_id)) continue;

        saw_resource = true;

        if (!g->active) {
            saw_revoked = true;
            continue;
        }
        if (g->epoch != engine->epoch) {
            saw_epoch = true;
            continue;
        }
        if (request->now_ns < g->not_before_ns) {
            saw_not_yet = true;
            continue;
        }
        if (g->expires_at_ns != 0 && request->now_ns >= g->expires_at_ns) {
            saw_expired = true;
            continue;
        }
        if (g->effect != AI_POLICY_EFFECT_ALLOW) continue;
        if ((g->rights & request->rights) != request->rights) {
            saw_rights = true;
            continue;
        }
        if (g->byte_limit != 0 && request->bytes > g->byte_limit) {
            saw_bytes = true;
            continue;
        }
        if (request->device_quota_bps > g->device_quota_bps) {
            saw_quota = true;
            continue;
        }

        ai_policy_result r = result(true, AI_POLICY_REASON_ALLOWED, g->id);
        audit_record(engine, AI_AUDIT_AUTHORIZE, request->subject_id,
                     request->resource_kind, request->resource_id,
                     request->rights, true, r.reason, g->id);
        return r;
    }

    ai_policy_reason reason = AI_POLICY_REASON_DENY_NO_MATCH;
    if (saw_revoked) reason = AI_POLICY_REASON_DENY_REVOKED;
    else if (saw_epoch) reason = AI_POLICY_REASON_DENY_EPOCH;
    else if (saw_not_yet) reason = AI_POLICY_REASON_DENY_NOT_YET_VALID;
    else if (saw_expired) reason = AI_POLICY_REASON_DENY_EXPIRED;
    else if (saw_rights) reason = AI_POLICY_REASON_DENY_RIGHTS;
    else if (saw_bytes) reason = AI_POLICY_REASON_DENY_BYTE_LIMIT;
    else if (saw_quota) reason = AI_POLICY_REASON_DENY_QUOTA;
    else if (!saw_resource) reason = AI_POLICY_REASON_DENY_NO_MATCH;

    ai_policy_result r = result(false, reason, 0);
    audit_record(engine, AI_AUDIT_AUTHORIZE, request->subject_id,
                 request->resource_kind, request->resource_id,
                 request->rights, false, r.reason, 0);
    return r;
}

ai_policy_result ai_policy_delegate(
    ai_policy_engine *engine,
    u64 parent_grant_id,
    u64 delegator_subject_id,
    u64 now_ns,
    const ai_policy_delegation *delegation
) {
    if (!engine || !delegation || delegation->rights == 0 ||
        delegation->device_quota_bps == 0 ||
        delegation->device_quota_bps > AI_POLICY_QUOTA_FULL_BPS) {
        ai_policy_result r = result(false, AI_POLICY_REASON_DENY_INVALID_REQUEST, parent_grant_id);
        if (engine && delegation) {
            audit_record(engine, AI_AUDIT_DELEGATE, delegator_subject_id,
                         delegation->resource_kind, delegation->resource_id,
                         delegation->rights, false, r.reason, parent_grant_id);
        }
        return r;
    }

    ai_policy_grant *parent = find_grant_mut(engine, parent_grant_id);
    if (!parent || parent->effect != AI_POLICY_EFFECT_ALLOW ||
        parent->subject_id != delegator_subject_id || !parent->active ||
        parent->epoch != engine->epoch || !time_active(parent, now_ns)) {
        ai_policy_result r = result(false, AI_POLICY_REASON_DENY_DELEGATION, parent_grant_id);
        audit_record(engine, AI_AUDIT_DELEGATE, delegator_subject_id,
                     delegation->resource_kind, delegation->resource_id,
                     delegation->rights, false, r.reason, parent_grant_id);
        return r;
    }

    if (parent->delegation_depth >= parent->max_delegation_depth ||
        (delegation->rights & parent->rights) != delegation->rights ||
        !resource_narrows(parent->resource_kind, parent->resource_id,
                          delegation->resource_kind, delegation->resource_id) ||
        !time_narrows(parent, now_ns, delegation->not_before_ns,
                      delegation->expires_at_ns) ||
        !bytes_narrow(parent->byte_limit, delegation->byte_limit) ||
        delegation->device_quota_bps > parent->device_quota_bps) {
        ai_policy_result r = result(false, AI_POLICY_REASON_DENY_DELEGATION, parent_grant_id);
        audit_record(engine, AI_AUDIT_DELEGATE, delegator_subject_id,
                     delegation->resource_kind, delegation->resource_id,
                     delegation->rights, false, r.reason, parent_grant_id);
        return r;
    }

    ai_policy_grant *child = allocate_grant_slot(engine);
    if (!child) {
        ai_policy_result r = result(false, AI_POLICY_REASON_DENY_TABLE_FULL, parent_grant_id);
        audit_record(engine, AI_AUDIT_DELEGATE, delegator_subject_id,
                     delegation->resource_kind, delegation->resource_id,
                     delegation->rights, false, r.reason, parent_grant_id);
        return r;
    }

    child->id = engine->next_grant_id++;
    child->parent_id = parent->id;
    child->subject_id = delegation->child_subject_id;
    child->issuer_id = parent->subject_id;
    child->effect = AI_POLICY_EFFECT_ALLOW;
    child->resource_kind = delegation->resource_kind;
    child->resource_id = delegation->resource_id;
    child->rights = delegation->rights;
    child->not_before_ns = delegation->not_before_ns;
    child->expires_at_ns = delegation->expires_at_ns;
    child->byte_limit = delegation->byte_limit;
    child->device_quota_bps = delegation->device_quota_bps;
    child->delegation_depth = (u16)(parent->delegation_depth + 1u);
    child->max_delegation_depth = parent->max_delegation_depth;
    child->epoch = engine->epoch;
    child->active = true;

    ai_policy_result r = result(true, AI_POLICY_REASON_ALLOWED, child->id);
    audit_record(engine, AI_AUDIT_DELEGATE, delegator_subject_id,
                 delegation->resource_kind, delegation->resource_id,
                 delegation->rights, true, r.reason, child->id);
    return r;
}

static bool grant_parent_inactive(const ai_policy_engine *engine, const ai_policy_grant *grant) {
    if (grant->id == 0 || !grant->active || grant->parent_id == 0) return false;
    const ai_policy_grant *parent = ai_policy_find_grant(engine, grant->parent_id);
    return !parent || !parent->active;
}

bool ai_policy_revoke(ai_policy_engine *engine, u64 grant_id, u64 actor_subject_id) {
    ai_policy_grant *grant = find_grant_mut(engine, grant_id);
    if (!grant || !grant->active) {
        if (engine) {
            audit_record(engine, AI_AUDIT_REVOKE, actor_subject_id,
                         AI_RESOURCE_ANY, AI_POLICY_ANY_ID, 0,
                         false, AI_POLICY_REASON_DENY_REVOKED, grant_id);
        }
        return false;
    }

    grant->active = false;

    /* Cascading revocation preserves the no-orphaned-delegation invariant. */
    bool changed = true;
    while (changed) {
        changed = false;
        for (u32 i = 0; i < engine->grant_count; ++i) {
            if (grant_parent_inactive(engine, &engine->grants[i])) {
                engine->grants[i].active = false;
                changed = true;
            }
        }
    }

    audit_record(engine, AI_AUDIT_REVOKE, actor_subject_id,
                 grant->resource_kind, grant->resource_id, grant->rights,
                 true, AI_POLICY_REASON_ALLOWED, grant_id);
    return true;
}

u64 ai_policy_bump_epoch(ai_policy_engine *engine, u64 actor_subject_id) {
    if (!engine) return 0;
    for (u32 i = 0; i < engine->grant_count; ++i) {
        engine->grants[i].active = false;
    }
    /* Epoch transition is a global invalidation barrier; stale slots are reclaimable. */
    (void)ai_policy_reclaim_inactive(engine);
    engine->epoch++;
    if (engine->epoch == 0) engine->epoch = 1;

    audit_record(engine, AI_AUDIT_EPOCH_BUMP, actor_subject_id,
                 AI_RESOURCE_ANY, AI_POLICY_ANY_ID, 0,
                 true, AI_POLICY_REASON_ALLOWED, 0);
    return engine->epoch;
}

static ai_policy_validation validation(
    bool valid,
    ai_policy_invariant_error error,
    u64 grant_id
) {
    ai_policy_validation v;
    v.valid = valid;
    v.error = error;
    v.grant_id = grant_id;
    return v;
}

ai_policy_validation ai_policy_validate(const ai_policy_engine *engine) {
    if (!engine) return validation(false, AI_POLICY_INVARIANT_BAD_ID, 0);

    for (u32 i = 0; i < engine->grant_count; ++i) {
        const ai_policy_grant *g = &engine->grants[i];
        if (g->id == 0) {
            if (g->active) return validation(false, AI_POLICY_INVARIANT_BAD_ID, 0);
            continue; /* reusable hole */
        }
        for (u32 j = i + 1; j < engine->grant_count; ++j) {
            if (engine->grants[j].id != 0 && g->id == engine->grants[j].id) {
                return validation(false, AI_POLICY_INVARIANT_DUPLICATE_ID, g->id);
            }
        }
        if (!valid_effect(g->effect)) {
            return validation(false, AI_POLICY_INVARIANT_BAD_EFFECT, g->id);
        }
        if (g->rights == 0) return validation(false, AI_POLICY_INVARIANT_ZERO_RIGHTS, g->id);
        if (!resource_spec_valid(g->resource_kind, g->resource_id)) {
            return validation(false, AI_POLICY_INVARIANT_BAD_RESOURCE, g->id);
        }
        if (g->expires_at_ns != 0 && g->expires_at_ns <= g->not_before_ns) {
            return validation(false, AI_POLICY_INVARIANT_BAD_TIME_WINDOW, g->id);
        }
        if (g->device_quota_bps == 0 || g->device_quota_bps > AI_POLICY_QUOTA_FULL_BPS) {
            return validation(false, AI_POLICY_INVARIANT_BAD_QUOTA, g->id);
        }
        if (g->delegation_depth > g->max_delegation_depth) {
            return validation(false, AI_POLICY_INVARIANT_BAD_DEPTH, g->id);
        }

        if (g->parent_id == 0) continue;

        const ai_policy_grant *p = ai_policy_find_grant(engine, g->parent_id);
        if (!p) return validation(false, AI_POLICY_INVARIANT_PARENT_MISSING, g->id);
        if (p->effect != AI_POLICY_EFFECT_ALLOW || g->effect != AI_POLICY_EFFECT_ALLOW) {
            return validation(false, AI_POLICY_INVARIANT_PARENT_NOT_ALLOW, g->id);
        }
        if (g->active && !p->active) {
            return validation(false, AI_POLICY_INVARIANT_PARENT_INACTIVE, g->id);
        }
        if (g->issuer_id != p->subject_id) {
            return validation(false, AI_POLICY_INVARIANT_ISSUER_MISMATCH, g->id);
        }
        if ((g->rights & p->rights) != g->rights) {
            return validation(false, AI_POLICY_INVARIANT_RIGHTS_AMPLIFIED, g->id);
        }
        if (!resource_narrows(p->resource_kind, p->resource_id,
                              g->resource_kind, g->resource_id)) {
            return validation(false, AI_POLICY_INVARIANT_RESOURCE_WIDENED, g->id);
        }
        if (g->not_before_ns < p->not_before_ns ||
            (p->expires_at_ns != 0 &&
             (g->expires_at_ns == 0 || g->expires_at_ns > p->expires_at_ns))) {
            return validation(false, AI_POLICY_INVARIANT_TIME_WIDENED, g->id);
        }
        if (!bytes_narrow(p->byte_limit, g->byte_limit)) {
            return validation(false, AI_POLICY_INVARIANT_BYTES_WIDENED, g->id);
        }
        if (g->device_quota_bps > p->device_quota_bps) {
            return validation(false, AI_POLICY_INVARIANT_QUOTA_WIDENED, g->id);
        }
        if (g->delegation_depth != (u16)(p->delegation_depth + 1u) ||
            g->max_delegation_depth != p->max_delegation_depth) {
            return validation(false, AI_POLICY_INVARIANT_BAD_DEPTH, g->id);
        }
        if (g->epoch != p->epoch) {
            return validation(false, AI_POLICY_INVARIANT_EPOCH_MISMATCH, g->id);
        }
    }

    return validation(true, AI_POLICY_INVARIANT_OK, 0);
}

u32 ai_policy_audit_count(const ai_policy_engine *engine) {
    return engine ? engine->audit_count : 0;
}

bool ai_policy_audit_get(
    const ai_policy_engine *engine,
    u32 logical_index,
    ai_policy_audit_entry *out
) {
    if (!engine || !out || logical_index >= engine->audit_count) return false;
    u32 oldest = (engine->audit_next + AI_POLICY_AUDIT_CAPACITY - engine->audit_count)
                 % AI_POLICY_AUDIT_CAPACITY;
    u32 index = (oldest + logical_index) % AI_POLICY_AUDIT_CAPACITY;
    *out = engine->audit[index];
    return true;
}

const char *ai_policy_reason_name(ai_policy_reason reason) {
    switch (reason) {
        case AI_POLICY_REASON_ALLOWED: return "ALLOWED";
        case AI_POLICY_REASON_DENY_EXPLICIT: return "DENY_EXPLICIT";
        case AI_POLICY_REASON_DENY_NO_MATCH: return "DENY_NO_MATCH";
        case AI_POLICY_REASON_DENY_RIGHTS: return "DENY_RIGHTS";
        case AI_POLICY_REASON_DENY_NOT_YET_VALID: return "DENY_NOT_YET_VALID";
        case AI_POLICY_REASON_DENY_EXPIRED: return "DENY_EXPIRED";
        case AI_POLICY_REASON_DENY_BYTE_LIMIT: return "DENY_BYTE_LIMIT";
        case AI_POLICY_REASON_DENY_QUOTA: return "DENY_QUOTA";
        case AI_POLICY_REASON_DENY_REVOKED: return "DENY_REVOKED";
        case AI_POLICY_REASON_DENY_EPOCH: return "DENY_EPOCH";
        case AI_POLICY_REASON_DENY_DELEGATION: return "DENY_DELEGATION";
        case AI_POLICY_REASON_DENY_INVALID_REQUEST: return "DENY_INVALID_REQUEST";
        case AI_POLICY_REASON_DENY_TABLE_FULL: return "DENY_TABLE_FULL";
        default: return "UNKNOWN";
    }
}

const char *ai_resource_kind_name(ai_resource_kind kind) {
    switch (kind) {
        case AI_RESOURCE_ANY: return "ANY";
        case AI_RESOURCE_TENSOR: return "TENSOR";
        case AI_RESOURCE_MODEL: return "MODEL";
        case AI_RESOURCE_DEVICE: return "DEVICE";
        case AI_RESOURCE_NETWORK_ENDPOINT: return "NETWORK_ENDPOINT";
        case AI_RESOURCE_REMOTE_TENSOR: return "REMOTE_TENSOR";
        case AI_RESOURCE_COLLECTIVE: return "COLLECTIVE";
        default: return "UNKNOWN";
    }
}

const char *ai_policy_effect_name(ai_policy_effect effect) {
    switch (effect) {
        case AI_POLICY_EFFECT_ALLOW: return "ALLOW";
        case AI_POLICY_EFFECT_DENY: return "DENY";
        default: return "UNKNOWN";
    }
}

const char *ai_audit_action_name(ai_audit_action action) {
    switch (action) {
        case AI_AUDIT_AUTHORIZE: return "AUTHORIZE";
        case AI_AUDIT_DELEGATE: return "DELEGATE";
        case AI_AUDIT_REVOKE: return "REVOKE";
        case AI_AUDIT_EPOCH_BUMP: return "EPOCH_BUMP";
        default: return "UNKNOWN";
    }
}

const char *ai_policy_invariant_name(ai_policy_invariant_error error) {
    switch (error) {
        case AI_POLICY_INVARIANT_OK: return "OK";
        case AI_POLICY_INVARIANT_BAD_ID: return "BAD_ID";
        case AI_POLICY_INVARIANT_DUPLICATE_ID: return "DUPLICATE_ID";
        case AI_POLICY_INVARIANT_BAD_EFFECT: return "BAD_EFFECT";
        case AI_POLICY_INVARIANT_ZERO_RIGHTS: return "ZERO_RIGHTS";
        case AI_POLICY_INVARIANT_BAD_RESOURCE: return "BAD_RESOURCE";
        case AI_POLICY_INVARIANT_BAD_TIME_WINDOW: return "BAD_TIME_WINDOW";
        case AI_POLICY_INVARIANT_BAD_QUOTA: return "BAD_QUOTA";
        case AI_POLICY_INVARIANT_BAD_DEPTH: return "BAD_DEPTH";
        case AI_POLICY_INVARIANT_PARENT_MISSING: return "PARENT_MISSING";
        case AI_POLICY_INVARIANT_PARENT_NOT_ALLOW: return "PARENT_NOT_ALLOW";
        case AI_POLICY_INVARIANT_PARENT_INACTIVE: return "PARENT_INACTIVE";
        case AI_POLICY_INVARIANT_ISSUER_MISMATCH: return "ISSUER_MISMATCH";
        case AI_POLICY_INVARIANT_RIGHTS_AMPLIFIED: return "RIGHTS_AMPLIFIED";
        case AI_POLICY_INVARIANT_RESOURCE_WIDENED: return "RESOURCE_WIDENED";
        case AI_POLICY_INVARIANT_TIME_WIDENED: return "TIME_WIDENED";
        case AI_POLICY_INVARIANT_BYTES_WIDENED: return "BYTES_WIDENED";
        case AI_POLICY_INVARIANT_QUOTA_WIDENED: return "QUOTA_WIDENED";
        case AI_POLICY_INVARIANT_EPOCH_MISMATCH: return "EPOCH_MISMATCH";
        default: return "UNKNOWN";
    }
}
