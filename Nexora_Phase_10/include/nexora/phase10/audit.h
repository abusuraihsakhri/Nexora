#ifndef NEXORA_PHASE10_AUDIT_H
#define NEXORA_PHASE10_AUDIT_H

#include "p10_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nx_audit_event_type {
    NX_AUDIT_HEALTH_TRANSITION = 1,
    NX_AUDIT_RECOVERY_DECISION,
    NX_AUDIT_RECOVERY_RESULT,
    NX_AUDIT_CHECKPOINT_PREPARE,
    NX_AUDIT_CHECKPOINT_COMMIT,
    NX_AUDIT_SECURITY_DENIAL,
    NX_AUDIT_NODE_SELECTION
} nx_audit_event_type_t;

typedef struct nx_audit_event {
    uint64_t seq;
    nx_p10_time_t time;
    nx_audit_event_type_t type;
    nx_p10_id_t subject_id;
    nx_p10_id_t actor_id;
    int32_t code;
    uint64_t data0;
    uint64_t data1;
} nx_audit_event_t;

typedef struct nx_audit_log {
    nx_audit_event_t entries[NX_P10_AUDIT_CAPACITY];
    uint64_t next_seq;
} nx_audit_log_t;

void nx_audit_init(nx_audit_log_t *log);
void nx_audit_write(nx_audit_log_t *log,
                    nx_p10_time_t time,
                    nx_audit_event_type_t type,
                    nx_p10_id_t subject_id,
                    nx_p10_id_t actor_id,
                    int32_t code,
                    uint64_t data0,
                    uint64_t data1);
size_t nx_audit_snapshot(const nx_audit_log_t *log,
                         nx_audit_event_t *out,
                         size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif
