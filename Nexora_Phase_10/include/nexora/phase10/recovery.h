#ifndef NEXORA_PHASE10_RECOVERY_H
#define NEXORA_PHASE10_RECOVERY_H

#include "health.h"
#include "security.h"
#include "audit.h"
#include "checkpoint.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nx_failure_class {
    NX_FAILURE_TRANSIENT = 0,
    NX_FAILURE_PROCESS,
    NX_FAILURE_ACCELERATOR,
    NX_FAILURE_MEMORY,
    NX_FAILURE_NODE,
    NX_FAILURE_INTEGRITY,
    NX_FAILURE_KERNEL_CRITICAL
} nx_failure_class_t;

typedef enum nx_recovery_action {
    NX_RECOVERY_NONE = 0,
    NX_RECOVERY_RETRY,
    NX_RECOVERY_RESTART,
    NX_RECOVERY_ROLLBACK,
    NX_RECOVERY_MIGRATE,
    NX_RECOVERY_QUARANTINE,
    NX_RECOVERY_PANIC
} nx_recovery_action_t;

typedef struct nx_recovery_policy {
    uint32_t retry_limit;
    uint32_t restart_limit;
    uint32_t rollback_after;
    uint32_t quarantine_after;
} nx_recovery_policy_t;

typedef struct nx_recovery_decision {
    nx_recovery_action_t action;
    nx_p10_id_t subject_id;
    uint64_t checkpoint_id;
    nx_capset_t required_capability;
} nx_recovery_decision_t;

typedef nx_p10_status_t (*nx_recovery_executor_fn)(nx_recovery_action_t action,
                                                    nx_p10_id_t subject_id,
                                                    uint64_t checkpoint_id,
                                                    void *opaque);

typedef struct nx_recovery_engine {
    nx_recovery_policy_t policy;
    nx_health_registry_t *health;
    nx_checkpoint_store_t *checkpoints;
    nx_audit_log_t *audit;
    nx_recovery_executor_fn executor;
    void *executor_opaque;
} nx_recovery_engine_t;

void nx_recovery_init(nx_recovery_engine_t *engine,
                      nx_recovery_policy_t policy,
                      nx_health_registry_t *health,
                      nx_checkpoint_store_t *checkpoints,
                      nx_audit_log_t *audit,
                      nx_recovery_executor_fn executor,
                      void *executor_opaque);

nx_recovery_decision_t nx_recovery_decide(nx_recovery_engine_t *engine,
                                           nx_p10_id_t subject_id,
                                           nx_failure_class_t failure);

nx_p10_status_t nx_recovery_execute(nx_recovery_engine_t *engine,
                                     const nx_security_context_t *security,
                                     nx_p10_time_t now,
                                     nx_recovery_decision_t decision);

#ifdef __cplusplus
}
#endif

#endif
