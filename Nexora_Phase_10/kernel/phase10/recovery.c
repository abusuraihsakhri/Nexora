#include "nexora/phase10/recovery.h"

static int action_valid(nx_recovery_action_t action)
{
    return action >= NX_RECOVERY_NONE && action <= NX_RECOVERY_PANIC;
}

static int action_needs_executor(nx_recovery_action_t action)
{
    return action == NX_RECOVERY_RETRY || action == NX_RECOVERY_RESTART ||
           action == NX_RECOVERY_ROLLBACK || action == NX_RECOVERY_MIGRATE ||
           action == NX_RECOVERY_PANIC;
}

static nx_capset_t cap_for_action(nx_recovery_action_t action)
{
    switch (action) {
        case NX_RECOVERY_RESTART: return NX_CAP_RESTART_COMPONENT;
        case NX_RECOVERY_ROLLBACK: return NX_CAP_ROLLBACK;
        case NX_RECOVERY_MIGRATE: return NX_CAP_MIGRATE;
        case NX_RECOVERY_QUARANTINE: return NX_CAP_QUARANTINE;
        case NX_RECOVERY_PANIC: return NX_CAP_SYSTEM_PANIC;
        default: return 0;
    }
}

void nx_recovery_init(nx_recovery_engine_t *engine,
                      nx_recovery_policy_t policy,
                      nx_health_registry_t *health,
                      nx_checkpoint_store_t *checkpoints,
                      nx_audit_log_t *audit,
                      nx_recovery_executor_fn executor,
                      void *executor_opaque)
{
    if (!engine) return;
    nx_p10_memzero(engine, sizeof(*engine));
    engine->policy = policy;
    engine->health = health;
    engine->checkpoints = checkpoints;
    engine->audit = audit;
    engine->executor = executor;
    engine->executor_opaque = executor_opaque;
}

nx_recovery_decision_t nx_recovery_decide(nx_recovery_engine_t *engine,
                                           nx_p10_id_t subject_id,
                                           nx_failure_class_t failure)
{
    nx_recovery_decision_t d;
    nx_component_health_t *health;
    const nx_checkpoint_t *checkpoint;
    nx_p10_memzero(&d, sizeof(d));
    d.subject_id = subject_id;
    if (!engine || !engine->health) return d;
    health = nx_health_get(engine->health, subject_id);
    if (!health) return d;

    checkpoint = engine->checkpoints ? nx_checkpoint_latest(engine->checkpoints, subject_id) : NULL;

    switch (failure) {
        case NX_FAILURE_TRANSIENT:
            d.action = health->recovery_attempts < engine->policy.retry_limit
                         ? NX_RECOVERY_RETRY : NX_RECOVERY_RESTART;
            break;
        case NX_FAILURE_PROCESS:
            if (checkpoint && engine->policy.rollback_after != 0u &&
                health->recovery_attempts >= engine->policy.rollback_after)
                d.action = NX_RECOVERY_ROLLBACK;
            else if (health->recovery_attempts < engine->policy.restart_limit)
                d.action = NX_RECOVERY_RESTART;
            else if (checkpoint)
                d.action = NX_RECOVERY_ROLLBACK;
            else
                d.action = NX_RECOVERY_QUARANTINE;
            break;
        case NX_FAILURE_ACCELERATOR:
        case NX_FAILURE_NODE:
            d.action = NX_RECOVERY_MIGRATE;
            break;
        case NX_FAILURE_MEMORY:
            d.action = checkpoint ? NX_RECOVERY_ROLLBACK : NX_RECOVERY_RESTART;
            break;
        case NX_FAILURE_INTEGRITY:
            d.action = NX_RECOVERY_QUARANTINE;
            break;
        case NX_FAILURE_KERNEL_CRITICAL:
            d.action = NX_RECOVERY_PANIC;
            break;
        default:
            d.action = NX_RECOVERY_NONE;
            break;
    }

    if (engine->policy.quarantine_after != 0u &&
        health->recovery_attempts >= engine->policy.quarantine_after &&
        d.action != NX_RECOVERY_PANIC)
        d.action = NX_RECOVERY_QUARANTINE;

    if (checkpoint && (d.action == NX_RECOVERY_ROLLBACK || d.action == NX_RECOVERY_MIGRATE))
        d.checkpoint_id = checkpoint->checkpoint_id;
    d.required_capability = cap_for_action(d.action);
    return d;
}

nx_p10_status_t nx_recovery_execute(nx_recovery_engine_t *engine,
                                     const nx_security_context_t *security,
                                     nx_p10_time_t now,
                                     nx_recovery_decision_t decision)
{
    nx_component_health_t *health;
    nx_p10_status_t rc = NX_P10_OK;
    nx_capset_t required;
    if (!engine || !engine->health || decision.subject_id == 0)
        return NX_P10_EINVAL;
    if (!action_valid(decision.action))
        return NX_P10_EINVAL;
    health = nx_health_get(engine->health, decision.subject_id);
    if (!health) return NX_P10_ENOENT;

    /* Never trust a caller-supplied capability field: derive it from the action. */
    required = cap_for_action(decision.action);
    if (required && !nx_security_has(security, required)) {
        nx_audit_write(engine->audit, now, NX_AUDIT_SECURITY_DENIAL,
                       decision.subject_id,
                       security ? security->principal_id : 0,
                       NX_P10_EPERM,
                       (uint64_t)decision.action,
                       required);
        return NX_P10_EPERM;
    }

    /* Reject stale, cross-owner or corrupted checkpoint references. */
    if (decision.action == NX_RECOVERY_ROLLBACK) {
        if (!engine->checkpoints ||
            !nx_checkpoint_get_committed(engine->checkpoints, decision.checkpoint_id,
                                         decision.subject_id))
            return NX_P10_ECORRUPT;
    } else if (decision.action == NX_RECOVERY_MIGRATE && decision.checkpoint_id != 0u) {
        if (!engine->checkpoints ||
            !nx_checkpoint_get_committed(engine->checkpoints, decision.checkpoint_id,
                                         decision.subject_id))
            return NX_P10_ECORRUPT;
    }

    nx_audit_write(engine->audit, now, NX_AUDIT_RECOVERY_DECISION,
                   decision.subject_id,
                   security ? security->principal_id : 0,
                   0,
                   (uint64_t)decision.action,
                   decision.checkpoint_id);

    if (decision.action == NX_RECOVERY_NONE)
        return NX_P10_OK;

    ++health->recovery_attempts;

    if (action_needs_executor(decision.action) && !engine->executor) {
        health->state = NX_HEALTH_FAILED;
        rc = NX_P10_ESTATE;
        nx_audit_write(engine->audit, now, NX_AUDIT_RECOVERY_RESULT,
                       decision.subject_id,
                       security ? security->principal_id : 0,
                       rc,
                       (uint64_t)decision.action,
                       health->recovery_attempts);
        return rc;
    }

    if (decision.action == NX_RECOVERY_QUARANTINE)
        health->state = NX_HEALTH_QUARANTINED;
    else if (decision.action != NX_RECOVERY_PANIC)
        health->state = NX_HEALTH_RECOVERING;

    if (engine->executor)
        rc = engine->executor(decision.action, decision.subject_id,
                              decision.checkpoint_id, engine->executor_opaque);

    if (rc == NX_P10_OK && decision.action != NX_RECOVERY_QUARANTINE &&
        decision.action != NX_RECOVERY_PANIC) {
        health->state = NX_HEALTH_HEALTHY;
        health->consecutive_failures = 0;
        ++health->generation;
    } else if (rc != NX_P10_OK && decision.action != NX_RECOVERY_QUARANTINE) {
        health->state = NX_HEALTH_FAILED;
    }

    nx_audit_write(engine->audit, now, NX_AUDIT_RECOVERY_RESULT,
                   decision.subject_id,
                   security ? security->principal_id : 0,
                   rc,
                   (uint64_t)decision.action,
                   health->recovery_attempts);
    return rc;
}
