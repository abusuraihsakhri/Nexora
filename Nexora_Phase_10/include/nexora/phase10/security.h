#ifndef NEXORA_PHASE10_SECURITY_H
#define NEXORA_PHASE10_SECURITY_H

#include "p10_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t nx_capset_t;

enum nx_capability {
    NX_CAP_HEALTH_ADMIN       = 1ull << 0,
    NX_CAP_RESTART_COMPONENT  = 1ull << 1,
    NX_CAP_ROLLBACK           = 1ull << 2,
    NX_CAP_MIGRATE            = 1ull << 3,
    NX_CAP_QUARANTINE         = 1ull << 4,
    NX_CAP_CHECKPOINT_WRITE   = 1ull << 5,
    NX_CAP_AUDIT_READ         = 1ull << 6,
    NX_CAP_SYSTEM_PANIC       = 1ull << 7
};

typedef struct nx_security_context {
    nx_p10_id_t principal_id;
    nx_capset_t capabilities;
} nx_security_context_t;

static inline int nx_security_has(const nx_security_context_t *ctx,
                                  nx_capset_t required)
{
    return ctx != NULL && (ctx->capabilities & required) == required;
}

#ifdef __cplusplus
}
#endif

#endif
