#include <ai/reliability.h>

static u32 rel_max_u32(u32 a, u32 b) {
    return a > b ? a : b;
}

static ai_rel_policy rel_sanitize_policy(ai_rel_policy p) {
    if (p.degrade_after_failures == 0u) {
        p.degrade_after_failures = 1u;
    }
    if (p.quarantine_after_failures < p.degrade_after_failures) {
        p.quarantine_after_failures = p.degrade_after_failures;
    }
    if (p.fail_after_failures < p.quarantine_after_failures) {
        p.fail_after_failures = p.quarantine_after_failures;
    }
    if (p.max_retry_attempts == 0u) {
        p.max_retry_attempts = 1u;
    }
    if (p.quarantine_ns == 0ull) {
        p.quarantine_ns = 1000000000ull;
    }
    if (p.heartbeat_timeout_ns == 0ull) {
        p.heartbeat_timeout_ns = 5000000000ull;
    }
    if (p.safe_mode_failed_domains == 0u) {
        p.safe_mode_failed_domains = 1u;
    }
    return p;
}

static bool rel_valid_domain_id(u64 domain_id) {
    return domain_id != AI_REL_INVALID_DOMAIN;
}

static void rel_zero_domain(ai_rel_domain *d) {
    d->id = 0ull;
    d->subject_id = 0ull;
    d->kind = AI_REL_DOMAIN_SERVICE;
    d->health = AI_REL_HEALTH_HEALTHY;
    d->generation = 0u;
    d->consecutive_failures = 0u;
    d->total_failures = 0u;
    d->total_recoveries = 0u;
    d->retry_attempts = 0u;
    d->last_fault = AI_REL_FAULT_NONE;
    d->last_severity = AI_REL_SEV_INFO;
    d->last_fault_ns = 0ull;
    d->last_heartbeat_ns = 0ull;
    d->quarantine_until_ns = 0ull;
}

static void rel_zero_event(ai_rel_event *e) {
    e->sequence = 0ull;
    e->timestamp_ns = 0ull;
    e->kind = AI_REL_EVENT_BOOT;
    e->actor_id = 0ull;
    e->object_id = 0ull;
    e->arg0 = 0ull;
    e->arg1 = 0ull;
}

ai_rel_policy ai_rel_default_policy(void) {
    ai_rel_policy p;
    p.degrade_after_failures = 1u;
    p.quarantine_after_failures = 3u;
    p.fail_after_failures = 6u;
    p.max_retry_attempts = 2u;
    p.quarantine_ns = 5000000000ull;      /* 5 s */
    p.heartbeat_timeout_ns = 3000000000ull; /* 3 s */
    p.safe_mode_failed_domains = 1u;
    return p;
}

u64 ai_rel_journal_append(
    ai_reliability *rel,
    ai_rel_event_kind kind,
    u64 actor_id,
    u64 object_id,
    u64 arg0,
    u64 arg1,
    u64 now_ns
) {
    ai_rel_event *slot;
    u32 index;

    if (rel == NULL) {
        return 0ull;
    }

    if (rel->journal.count < AI_REL_JOURNAL_CAPACITY) {
        index = (rel->journal.head + rel->journal.count) % AI_REL_JOURNAL_CAPACITY;
        rel->journal.count++;
    } else {
        index = rel->journal.head;
        rel->journal.head = (rel->journal.head + 1u) % AI_REL_JOURNAL_CAPACITY;
        rel->journal.dropped_events++;
    }

    slot = &rel->journal.events[index];
    slot->sequence = rel->journal.next_sequence++;
    slot->timestamp_ns = now_ns;
    slot->kind = kind;
    slot->actor_id = actor_id;
    slot->object_id = object_id;
    slot->arg0 = arg0;
    slot->arg1 = arg1;
    return slot->sequence;
}

void ai_rel_init(ai_reliability *rel, const ai_rel_policy *policy, u64 now_ns) {
    u32 i;
    ai_rel_policy p;

    if (rel == NULL) {
        return;
    }

    p = policy == NULL ? ai_rel_default_policy() : *policy;
    rel->policy = rel_sanitize_policy(p);
    rel->registry.count = 0u;
    rel->registry.next_id = 1ull;

    for (i = 0u; i < AI_REL_MAX_DOMAINS; ++i) {
        rel_zero_domain(&rel->registry.domains[i]);
    }

    rel->journal.head = 0u;
    rel->journal.count = 0u;
    rel->journal.next_sequence = 1ull;
    rel->journal.dropped_events = 0ull;
    for (i = 0u; i < AI_REL_JOURNAL_CAPACITY; ++i) {
        rel_zero_event(&rel->journal.events[i]);
    }

    rel->safe_mode = false;
    rel->next_checkpoint_id = 1ull;
    ai_rel_journal_append(rel, AI_REL_EVENT_BOOT, 0ull, 0ull, 0ull, 0ull, now_ns);
}

ai_rel_domain *ai_rel_domain_register(
    ai_reliability *rel,
    ai_rel_domain_kind kind,
    u64 subject_id,
    u64 now_ns
) {
    ai_rel_domain *d;

    if (rel == NULL || rel->registry.count >= AI_REL_MAX_DOMAINS) {
        return NULL;
    }

    d = &rel->registry.domains[rel->registry.count++];
    rel_zero_domain(d);
    d->id = rel->registry.next_id++;
    d->subject_id = subject_id;
    d->kind = kind;
    d->health = AI_REL_HEALTH_HEALTHY;
    d->generation = 1u;
    d->last_heartbeat_ns = now_ns;

    ai_rel_journal_append(
        rel,
        AI_REL_EVENT_DOMAIN_REGISTER,
        d->id,
        subject_id,
        (u64)kind,
        (u64)d->generation,
        now_ns
    );
    return d;
}

ai_rel_domain *ai_rel_domain_find(ai_reliability *rel, u64 domain_id) {
    u32 i;
    if (rel == NULL || !rel_valid_domain_id(domain_id)) {
        return NULL;
    }
    for (i = 0u; i < rel->registry.count; ++i) {
        if (rel->registry.domains[i].id == domain_id) {
            return &rel->registry.domains[i];
        }
    }
    return NULL;
}

const ai_rel_domain *ai_rel_domain_find_const(const ai_reliability *rel, u64 domain_id) {
    u32 i;
    if (rel == NULL || !rel_valid_domain_id(domain_id)) {
        return NULL;
    }
    for (i = 0u; i < rel->registry.count; ++i) {
        if (rel->registry.domains[i].id == domain_id) {
            return &rel->registry.domains[i];
        }
    }
    return NULL;
}

static void rel_transition(ai_reliability *rel, ai_rel_domain *d, ai_rel_health next, u64 now_ns) {
    ai_rel_health prev;
    if (rel == NULL || d == NULL || d->health == next) {
        return;
    }
    prev = d->health;
    d->health = next;
    ai_rel_journal_append(
        rel,
        AI_REL_EVENT_HEALTH_TRANSITION,
        d->id,
        d->subject_id,
        (u64)prev,
        (u64)next,
        now_ns
    );
}

bool ai_rel_heartbeat(ai_reliability *rel, u64 domain_id, u64 now_ns) {
    ai_rel_domain *d = ai_rel_domain_find(rel, domain_id);
    if (d == NULL) {
        return false;
    }

    d->last_heartbeat_ns = now_ns;
    if (d->last_fault == AI_REL_FAULT_HEARTBEAT_LOST) {
        d->last_fault = AI_REL_FAULT_NONE;
        d->last_severity = AI_REL_SEV_INFO;
    }
    ai_rel_journal_append(
        rel,
        AI_REL_EVENT_HEARTBEAT,
        d->id,
        d->subject_id,
        0ull,
        0ull,
        now_ns
    );
    return true;
}

bool ai_rel_record_fault(
    ai_reliability *rel,
    u64 domain_id,
    ai_rel_severity severity,
    ai_rel_fault_code code,
    u64 now_ns
) {
    ai_rel_domain *d = ai_rel_domain_find(rel, domain_id);
    u32 failure_count;

    if (d == NULL || code == AI_REL_FAULT_NONE) {
        return false;
    }

    d->last_fault = code;
    d->last_severity = severity;
    d->last_fault_ns = now_ns;

    if (severity >= AI_REL_SEV_WARNING) {
        d->total_failures++;
        d->consecutive_failures++;
    }

    ai_rel_journal_append(
        rel,
        AI_REL_EVENT_FAULT,
        d->id,
        d->subject_id,
        (u64)severity,
        (u64)code,
        now_ns
    );

    if (severity == AI_REL_SEV_FATAL) {
        rel_transition(rel, d, AI_REL_HEALTH_FAILED, now_ns);
        ai_rel_refresh_safe_mode(rel, now_ns);
        return true;
    }

    failure_count = d->consecutive_failures;
    if (severity >= AI_REL_SEV_ERROR && failure_count >= rel->policy.fail_after_failures) {
        rel_transition(rel, d, AI_REL_HEALTH_FAILED, now_ns);
    } else if (severity >= AI_REL_SEV_ERROR && failure_count >= rel->policy.quarantine_after_failures) {
        d->quarantine_until_ns = now_ns + rel->policy.quarantine_ns;
        rel_transition(rel, d, AI_REL_HEALTH_QUARANTINED, now_ns);
    } else if (severity >= AI_REL_SEV_WARNING && failure_count >= rel->policy.degrade_after_failures) {
        rel_transition(rel, d, AI_REL_HEALTH_DEGRADED, now_ns);
    }

    ai_rel_refresh_safe_mode(rel, now_ns);
    return true;
}

u32 ai_rel_check_heartbeats(ai_reliability *rel, u64 now_ns) {
    u32 i;
    u32 faults = 0u;

    if (rel == NULL) {
        return 0u;
    }

    for (i = 0u; i < rel->registry.count; ++i) {
        ai_rel_domain *d = &rel->registry.domains[i];
        u64 elapsed;

        if (d->health == AI_REL_HEALTH_FAILED || d->health == AI_REL_HEALTH_QUARANTINED) {
            continue;
        }
        if (now_ns < d->last_heartbeat_ns) {
            continue;
        }
        elapsed = now_ns - d->last_heartbeat_ns;
        if (elapsed > rel->policy.heartbeat_timeout_ns && d->last_fault != AI_REL_FAULT_HEARTBEAT_LOST) {
            if (ai_rel_record_fault(
                rel,
                d->id,
                AI_REL_SEV_ERROR,
                AI_REL_FAULT_HEARTBEAT_LOST,
                now_ns
            )) {
                faults++;
            }
        }
    }
    return faults;
}

static bool rel_is_network_fault(ai_rel_fault_code code) {
    return code == AI_REL_FAULT_REMOTE_UNREACHABLE ||
           code == AI_REL_FAULT_PROTOCOL_ERROR ||
           code == AI_REL_FAULT_CHECKSUM_MISMATCH;
}

static bool rel_is_device_fault(ai_rel_fault_code code) {
    return code == AI_REL_FAULT_DEVICE_RESET ||
           code == AI_REL_FAULT_DMA_ERROR;
}

bool ai_rel_can_admit(const ai_reliability *rel, u64 domain_id, bool critical_work, u64 now_ns) {
    const ai_rel_domain *d = ai_rel_domain_find_const(rel, domain_id);
    (void)now_ns;
    if (rel == NULL || d == NULL) {
        return false;
    }

    if (d->health == AI_REL_HEALTH_FAILED || d->health == AI_REL_HEALTH_RECOVERING) {
        return false;
    }
    if (d->health == AI_REL_HEALTH_QUARANTINED) {
        return false;
    }
    if (rel->safe_mode && !critical_work) {
        return false;
    }
    return true;
}

ai_rel_recovery_action ai_rel_choose_recovery(
    const ai_reliability *rel,
    u64 domain_id,
    ai_rel_fault_code code
) {
    const ai_rel_domain *d = ai_rel_domain_find_const(rel, domain_id);

    if (rel == NULL || d == NULL || code == AI_REL_FAULT_NONE) {
        return AI_REL_RECOVERY_NONE;
    }
    if (d->health == AI_REL_HEALTH_FAILED || d->last_severity == AI_REL_SEV_FATAL) {
        return AI_REL_RECOVERY_FAIL_DOMAIN;
    }
    if (code == AI_REL_FAULT_CAPABILITY_VIOLATION || code == AI_REL_FAULT_INTERNAL_INVARIANT) {
        return AI_REL_RECOVERY_QUARANTINE;
    }
    if (code == AI_REL_FAULT_RESOURCE_EXHAUSTION) {
        return AI_REL_RECOVERY_ABORT_WORK;
    }
    if (d->retry_attempts < rel->policy.max_retry_attempts) {
        return AI_REL_RECOVERY_RETRY;
    }
    if (rel_is_device_fault(code)) {
        return AI_REL_RECOVERY_RESET_RESOURCE;
    }
    if (rel_is_network_fault(code)) {
        return AI_REL_RECOVERY_QUARANTINE;
    }
    if (code == AI_REL_FAULT_WORK_FAILED || code == AI_REL_FAULT_SCHEDULER_STALL) {
        return AI_REL_RECOVERY_ROLLBACK_CHECKPOINT;
    }
    return AI_REL_RECOVERY_ABORT_WORK;
}

bool ai_rel_begin_recovery(ai_reliability *rel, u64 domain_id, ai_rel_recovery_action action, u64 now_ns) {
    ai_rel_domain *d = ai_rel_domain_find(rel, domain_id);
    if (d == NULL || action == AI_REL_RECOVERY_NONE) {
        return false;
    }

    ai_rel_journal_append(
        rel,
        AI_REL_EVENT_RECOVERY_DECISION,
        d->id,
        d->subject_id,
        (u64)action,
        (u64)d->last_fault,
        now_ns
    );

    if (action == AI_REL_RECOVERY_FAIL_DOMAIN) {
        rel_transition(rel, d, AI_REL_HEALTH_FAILED, now_ns);
        ai_rel_refresh_safe_mode(rel, now_ns);
        return true;
    }

    if (action == AI_REL_RECOVERY_QUARANTINE) {
        d->quarantine_until_ns = now_ns + rel->policy.quarantine_ns;
        rel_transition(rel, d, AI_REL_HEALTH_QUARANTINED, now_ns);
        ai_rel_refresh_safe_mode(rel, now_ns);
        return true;
    }

    d->retry_attempts++;
    rel_transition(rel, d, AI_REL_HEALTH_RECOVERING, now_ns);
    return true;
}

bool ai_rel_recovery_succeeded(ai_reliability *rel, u64 domain_id, u64 now_ns) {
    ai_rel_domain *d = ai_rel_domain_find(rel, domain_id);
    if (d == NULL) {
        return false;
    }

    d->consecutive_failures = 0u;
    d->retry_attempts = 0u;
    d->last_fault = AI_REL_FAULT_NONE;
    d->last_severity = AI_REL_SEV_INFO;
    d->quarantine_until_ns = 0ull;
    d->total_recoveries++;
    d->generation++;
    d->last_heartbeat_ns = now_ns;
    rel_transition(rel, d, AI_REL_HEALTH_HEALTHY, now_ns);
    ai_rel_refresh_safe_mode(rel, now_ns);
    return true;
}

bool ai_rel_recovery_failed(ai_reliability *rel, u64 domain_id, u64 now_ns) {
    ai_rel_domain *d = ai_rel_domain_find(rel, domain_id);
    ai_rel_recovery_action action;

    if (d == NULL) {
        return false;
    }

    d->total_failures++;
    d->consecutive_failures = rel_max_u32(d->consecutive_failures + 1u, 1u);
    action = ai_rel_choose_recovery(rel, domain_id, d->last_fault);

    if (d->consecutive_failures >= rel->policy.fail_after_failures || action == AI_REL_RECOVERY_FAIL_DOMAIN) {
        rel_transition(rel, d, AI_REL_HEALTH_FAILED, now_ns);
    } else {
        d->quarantine_until_ns = now_ns + rel->policy.quarantine_ns;
        rel_transition(rel, d, AI_REL_HEALTH_QUARANTINED, now_ns);
    }
    ai_rel_refresh_safe_mode(rel, now_ns);
    return true;
}

u32 ai_rel_release_expired_quarantines(ai_reliability *rel, u64 now_ns) {
    u32 i;
    u32 released = 0u;

    if (rel == NULL) {
        return 0u;
    }

    for (i = 0u; i < rel->registry.count; ++i) {
        ai_rel_domain *d = &rel->registry.domains[i];
        if (d->health == AI_REL_HEALTH_QUARANTINED && now_ns >= d->quarantine_until_ns) {
            d->retry_attempts = 0u;
            rel_transition(rel, d, AI_REL_HEALTH_DEGRADED, now_ns);
            released++;
        }
    }
    ai_rel_refresh_safe_mode(rel, now_ns);
    return released;
}

u32 ai_rel_journal_count(const ai_reliability *rel) {
    return rel == NULL ? 0u : rel->journal.count;
}

bool ai_rel_journal_get_oldest(const ai_reliability *rel, u32 index, ai_rel_event *out_event) {
    u32 physical;
    if (rel == NULL || out_event == NULL || index >= rel->journal.count) {
        return false;
    }
    physical = (rel->journal.head + index) % AI_REL_JOURNAL_CAPACITY;
    *out_event = rel->journal.events[physical];
    return true;
}

static u64 rel_fnv_mix_u64(u64 hash, u64 value) {
    u32 i;
    for (i = 0u; i < 8u; ++i) {
        u8 byte = (u8)((value >> (i * 8u)) & 0xffull);
        hash ^= (u64)byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

u64 ai_rel_journal_checksum(const ai_reliability *rel) {
    u64 hash = 1469598103934665603ull;
    u32 i;
    ai_rel_event e;

    if (rel == NULL) {
        return 0ull;
    }

    for (i = 0u; i < rel->journal.count; ++i) {
        if (!ai_rel_journal_get_oldest(rel, i, &e)) {
            break;
        }
        hash = rel_fnv_mix_u64(hash, e.sequence);
        hash = rel_fnv_mix_u64(hash, e.timestamp_ns);
        hash = rel_fnv_mix_u64(hash, (u64)e.kind);
        hash = rel_fnv_mix_u64(hash, e.actor_id);
        hash = rel_fnv_mix_u64(hash, e.object_id);
        hash = rel_fnv_mix_u64(hash, e.arg0);
        hash = rel_fnv_mix_u64(hash, e.arg1);
    }
    hash = rel_fnv_mix_u64(hash, rel->journal.dropped_events);
    return hash;
}

ai_rel_checkpoint ai_rel_checkpoint_capture(
    ai_reliability *rel,
    u64 graph_id,
    u64 graph_epoch,
    u64 scheduler_dispatch_count,
    u64 completed_work_count,
    u64 now_ns
) {
    ai_rel_checkpoint cp;
    cp.checkpoint_id = 0ull;
    cp.graph_id = graph_id;
    cp.graph_epoch = graph_epoch;
    cp.scheduler_dispatch_count = scheduler_dispatch_count;
    cp.completed_work_count = completed_work_count;
    cp.replay_sequence = 0ull;
    cp.journal_checksum = 0ull;
    cp.timestamp_ns = now_ns;

    if (rel == NULL) {
        return cp;
    }

    cp.checkpoint_id = rel->next_checkpoint_id++;
    cp.replay_sequence = rel->journal.next_sequence;
    cp.journal_checksum = ai_rel_journal_checksum(rel);

    ai_rel_journal_append(
        rel,
        AI_REL_EVENT_CHECKPOINT,
        graph_id,
        cp.checkpoint_id,
        graph_epoch,
        completed_work_count,
        now_ns
    );
    return cp;
}

bool ai_rel_safe_mode(const ai_reliability *rel) {
    return rel == NULL ? true : rel->safe_mode;
}

void ai_rel_refresh_safe_mode(ai_reliability *rel, u64 now_ns) {
    u32 i;
    u32 severe = 0u;
    bool previous;

    if (rel == NULL) {
        return;
    }

    previous = rel->safe_mode;
    for (i = 0u; i < rel->registry.count; ++i) {
        const ai_rel_domain *d = &rel->registry.domains[i];
        if (d->health == AI_REL_HEALTH_FAILED || d->health == AI_REL_HEALTH_QUARANTINED) {
            severe++;
        }
    }

    rel->safe_mode = severe >= rel->policy.safe_mode_failed_domains;
    if (rel->safe_mode != previous) {
        ai_rel_journal_append(
            rel,
            rel->safe_mode ? AI_REL_EVENT_SAFE_MODE_ENTER : AI_REL_EVENT_SAFE_MODE_EXIT,
            0ull,
            0ull,
            (u64)severe,
            (u64)rel->policy.safe_mode_failed_domains,
            now_ns
        );
    }
}

const char *ai_rel_health_name(ai_rel_health health) {
    switch (health) {
        case AI_REL_HEALTH_HEALTHY: return "HEALTHY";
        case AI_REL_HEALTH_DEGRADED: return "DEGRADED";
        case AI_REL_HEALTH_QUARANTINED: return "QUARANTINED";
        case AI_REL_HEALTH_RECOVERING: return "RECOVERING";
        case AI_REL_HEALTH_FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

const char *ai_rel_fault_name(ai_rel_fault_code code) {
    switch (code) {
        case AI_REL_FAULT_NONE: return "NONE";
        case AI_REL_FAULT_TIMEOUT: return "TIMEOUT";
        case AI_REL_FAULT_HEARTBEAT_LOST: return "HEARTBEAT_LOST";
        case AI_REL_FAULT_DEVICE_RESET: return "DEVICE_RESET";
        case AI_REL_FAULT_DMA_ERROR: return "DMA_ERROR";
        case AI_REL_FAULT_REMOTE_UNREACHABLE: return "REMOTE_UNREACHABLE";
        case AI_REL_FAULT_PROTOCOL_ERROR: return "PROTOCOL_ERROR";
        case AI_REL_FAULT_CHECKSUM_MISMATCH: return "CHECKSUM_MISMATCH";
        case AI_REL_FAULT_CAPABILITY_VIOLATION: return "CAPABILITY_VIOLATION";
        case AI_REL_FAULT_RESOURCE_EXHAUSTION: return "RESOURCE_EXHAUSTION";
        case AI_REL_FAULT_SCHEDULER_STALL: return "SCHEDULER_STALL";
        case AI_REL_FAULT_WORK_FAILED: return "WORK_FAILED";
        case AI_REL_FAULT_INTERNAL_INVARIANT: return "INTERNAL_INVARIANT";
        default: return "UNKNOWN";
    }
}

const char *ai_rel_recovery_name(ai_rel_recovery_action action) {
    switch (action) {
        case AI_REL_RECOVERY_NONE: return "NONE";
        case AI_REL_RECOVERY_RETRY: return "RETRY";
        case AI_REL_RECOVERY_RESET_RESOURCE: return "RESET_RESOURCE";
        case AI_REL_RECOVERY_ROLLBACK_CHECKPOINT: return "ROLLBACK_CHECKPOINT";
        case AI_REL_RECOVERY_QUARANTINE: return "QUARANTINE";
        case AI_REL_RECOVERY_ABORT_WORK: return "ABORT_WORK";
        case AI_REL_RECOVERY_FAIL_DOMAIN: return "FAIL_DOMAIN";
        default: return "UNKNOWN";
    }
}

const char *ai_rel_event_name(ai_rel_event_kind kind) {
    switch (kind) {
        case AI_REL_EVENT_BOOT: return "BOOT";
        case AI_REL_EVENT_DOMAIN_REGISTER: return "DOMAIN_REGISTER";
        case AI_REL_EVENT_HEARTBEAT: return "HEARTBEAT";
        case AI_REL_EVENT_FAULT: return "FAULT";
        case AI_REL_EVENT_HEALTH_TRANSITION: return "HEALTH_TRANSITION";
        case AI_REL_EVENT_SCHED_PICK: return "SCHED_PICK";
        case AI_REL_EVENT_RESOURCE_ASSIGN: return "RESOURCE_ASSIGN";
        case AI_REL_EVENT_WORK_START: return "WORK_START";
        case AI_REL_EVENT_WORK_COMPLETE: return "WORK_COMPLETE";
        case AI_REL_EVENT_WORK_FAIL: return "WORK_FAIL";
        case AI_REL_EVENT_RECOVERY_DECISION: return "RECOVERY_DECISION";
        case AI_REL_EVENT_CHECKPOINT: return "CHECKPOINT";
        case AI_REL_EVENT_SAFE_MODE_ENTER: return "SAFE_MODE_ENTER";
        case AI_REL_EVENT_SAFE_MODE_EXIT: return "SAFE_MODE_EXIT";
        default: return "UNKNOWN";
    }
}
