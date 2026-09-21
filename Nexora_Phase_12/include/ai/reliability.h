#ifndef NEXORA_AI_RELIABILITY_H
#define NEXORA_AI_RELIABILITY_H

#include <kernel/types.h>

#define AI_REL_MAX_DOMAINS       64u
#define AI_REL_JOURNAL_CAPACITY  256u
#define AI_REL_INVALID_DOMAIN    0ull

/*
 * Phase 12 reliability model.
 *
 * Design constraints:
 * - fixed memory only
 * - no libc
 * - no heap allocation
 * - time is supplied by the caller
 * - policy is explicit and deterministic
 * - intended to be called while the owning scheduler/resource lock is held
 */

typedef enum {
    AI_REL_DOMAIN_AGENT = 1,
    AI_REL_DOMAIN_DEVICE,
    AI_REL_DOMAIN_REMOTE_NODE,
    AI_REL_DOMAIN_NETWORK_PATH,
    AI_REL_DOMAIN_WORK_GRAPH,
    AI_REL_DOMAIN_SERVICE
} ai_rel_domain_kind;

typedef enum {
    AI_REL_HEALTH_HEALTHY = 0,
    AI_REL_HEALTH_DEGRADED,
    AI_REL_HEALTH_QUARANTINED,
    AI_REL_HEALTH_RECOVERING,
    AI_REL_HEALTH_FAILED
} ai_rel_health;

typedef enum {
    AI_REL_SEV_INFO = 0,
    AI_REL_SEV_WARNING,
    AI_REL_SEV_ERROR,
    AI_REL_SEV_FATAL
} ai_rel_severity;

typedef enum {
    AI_REL_FAULT_NONE = 0,
    AI_REL_FAULT_TIMEOUT,
    AI_REL_FAULT_HEARTBEAT_LOST,
    AI_REL_FAULT_DEVICE_RESET,
    AI_REL_FAULT_DMA_ERROR,
    AI_REL_FAULT_REMOTE_UNREACHABLE,
    AI_REL_FAULT_PROTOCOL_ERROR,
    AI_REL_FAULT_CHECKSUM_MISMATCH,
    AI_REL_FAULT_CAPABILITY_VIOLATION,
    AI_REL_FAULT_RESOURCE_EXHAUSTION,
    AI_REL_FAULT_SCHEDULER_STALL,
    AI_REL_FAULT_WORK_FAILED,
    AI_REL_FAULT_INTERNAL_INVARIANT
} ai_rel_fault_code;

typedef enum {
    AI_REL_RECOVERY_NONE = 0,
    AI_REL_RECOVERY_RETRY,
    AI_REL_RECOVERY_RESET_RESOURCE,
    AI_REL_RECOVERY_ROLLBACK_CHECKPOINT,
    AI_REL_RECOVERY_QUARANTINE,
    AI_REL_RECOVERY_ABORT_WORK,
    AI_REL_RECOVERY_FAIL_DOMAIN
} ai_rel_recovery_action;

typedef enum {
    AI_REL_EVENT_BOOT = 1,
    AI_REL_EVENT_DOMAIN_REGISTER,
    AI_REL_EVENT_HEARTBEAT,
    AI_REL_EVENT_FAULT,
    AI_REL_EVENT_HEALTH_TRANSITION,
    AI_REL_EVENT_SCHED_PICK,
    AI_REL_EVENT_RESOURCE_ASSIGN,
    AI_REL_EVENT_WORK_START,
    AI_REL_EVENT_WORK_COMPLETE,
    AI_REL_EVENT_WORK_FAIL,
    AI_REL_EVENT_RECOVERY_DECISION,
    AI_REL_EVENT_CHECKPOINT,
    AI_REL_EVENT_SAFE_MODE_ENTER,
    AI_REL_EVENT_SAFE_MODE_EXIT
} ai_rel_event_kind;

typedef struct {
    u32 degrade_after_failures;
    u32 quarantine_after_failures;
    u32 fail_after_failures;
    u32 max_retry_attempts;
    u64 quarantine_ns;
    u64 heartbeat_timeout_ns;
    u32 safe_mode_failed_domains;
} ai_rel_policy;

typedef struct {
    u64 id;
    u64 subject_id;
    ai_rel_domain_kind kind;
    ai_rel_health health;
    u32 generation;
    u32 consecutive_failures;
    u32 total_failures;
    u32 total_recoveries;
    u32 retry_attempts;
    ai_rel_fault_code last_fault;
    ai_rel_severity last_severity;
    u64 last_fault_ns;
    u64 last_heartbeat_ns;
    u64 quarantine_until_ns;
} ai_rel_domain;

typedef struct {
    ai_rel_domain domains[AI_REL_MAX_DOMAINS];
    u32 count;
    u64 next_id;
} ai_rel_registry;

typedef struct {
    u64 sequence;
    u64 timestamp_ns;
    ai_rel_event_kind kind;
    u64 actor_id;
    u64 object_id;
    u64 arg0;
    u64 arg1;
} ai_rel_event;

typedef struct {
    ai_rel_event events[AI_REL_JOURNAL_CAPACITY];
    u32 head;
    u32 count;
    u64 next_sequence;
    u64 dropped_events;
} ai_rel_journal;

typedef struct {
    u64 checkpoint_id;
    u64 graph_id;
    u64 graph_epoch;
    u64 scheduler_dispatch_count;
    u64 completed_work_count;
    u64 replay_sequence;
    u64 journal_checksum;
    u64 timestamp_ns;
} ai_rel_checkpoint;

typedef struct {
    ai_rel_registry registry;
    ai_rel_journal journal;
    ai_rel_policy policy;
    bool safe_mode;
    u64 next_checkpoint_id;
} ai_reliability;

/* Policy and initialization. */
ai_rel_policy ai_rel_default_policy(void);
void ai_rel_init(ai_reliability *rel, const ai_rel_policy *policy, u64 now_ns);

/* Fault-domain registry. */
ai_rel_domain *ai_rel_domain_register(
    ai_reliability *rel,
    ai_rel_domain_kind kind,
    u64 subject_id,
    u64 now_ns
);
ai_rel_domain *ai_rel_domain_find(ai_reliability *rel, u64 domain_id);
const ai_rel_domain *ai_rel_domain_find_const(const ai_reliability *rel, u64 domain_id);

/* Liveness and fault handling. */
bool ai_rel_heartbeat(ai_reliability *rel, u64 domain_id, u64 now_ns);
bool ai_rel_record_fault(
    ai_reliability *rel,
    u64 domain_id,
    ai_rel_severity severity,
    ai_rel_fault_code code,
    u64 now_ns
);
u32 ai_rel_check_heartbeats(ai_reliability *rel, u64 now_ns);

/* Admission and recovery. */
bool ai_rel_can_admit(const ai_reliability *rel, u64 domain_id, bool critical_work, u64 now_ns);
ai_rel_recovery_action ai_rel_choose_recovery(
    const ai_reliability *rel,
    u64 domain_id,
    ai_rel_fault_code code
);
bool ai_rel_begin_recovery(ai_reliability *rel, u64 domain_id, ai_rel_recovery_action action, u64 now_ns);
bool ai_rel_recovery_succeeded(ai_reliability *rel, u64 domain_id, u64 now_ns);
bool ai_rel_recovery_failed(ai_reliability *rel, u64 domain_id, u64 now_ns);
u32 ai_rel_release_expired_quarantines(ai_reliability *rel, u64 now_ns);

/* Deterministic execution journal. */
u64 ai_rel_journal_append(
    ai_reliability *rel,
    ai_rel_event_kind kind,
    u64 actor_id,
    u64 object_id,
    u64 arg0,
    u64 arg1,
    u64 now_ns
);
u32 ai_rel_journal_count(const ai_reliability *rel);
bool ai_rel_journal_get_oldest(const ai_reliability *rel, u32 index, ai_rel_event *out_event);
u64 ai_rel_journal_checksum(const ai_reliability *rel);

/* Checkpoint metadata: payload/data checkpointing is intentionally external. */
ai_rel_checkpoint ai_rel_checkpoint_capture(
    ai_reliability *rel,
    u64 graph_id,
    u64 graph_epoch,
    u64 scheduler_dispatch_count,
    u64 completed_work_count,
    u64 now_ns
);

/* Safe-mode state. */
bool ai_rel_safe_mode(const ai_reliability *rel);
void ai_rel_refresh_safe_mode(ai_reliability *rel, u64 now_ns);

/* Readable names for console/debug tooling. */
const char *ai_rel_health_name(ai_rel_health health);
const char *ai_rel_fault_name(ai_rel_fault_code code);
const char *ai_rel_recovery_name(ai_rel_recovery_action action);
const char *ai_rel_event_name(ai_rel_event_kind kind);

#endif
