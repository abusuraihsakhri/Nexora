#ifndef NEXORA_PHASE10_TYPES_H
#define NEXORA_PHASE10_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nx_p10_status {
    NX_P10_OK = 0,
    NX_P10_EINVAL = -1,
    NX_P10_ENOSPC = -2,
    NX_P10_ENOENT = -3,
    NX_P10_EPERM = -4,
    NX_P10_ESTATE = -5,
    NX_P10_ECORRUPT = -6
} nx_p10_status_t;

typedef uint64_t nx_p10_time_t;
typedef uint32_t nx_p10_id_t;

#ifndef NX_P10_MAX_COMPONENTS
#define NX_P10_MAX_COMPONENTS 128u
#endif

#ifndef NX_P10_MAX_CHECKPOINTS
#define NX_P10_MAX_CHECKPOINTS 64u
#endif

#ifndef NX_P10_AUDIT_CAPACITY
#define NX_P10_AUDIT_CAPACITY 256u
#endif

#ifndef NX_P10_MAX_NODES
#define NX_P10_MAX_NODES 64u
#endif

/* Keep Phase 10 independently freestanding: no libc memset dependency. */
static inline void nx_p10_memzero(void *ptr, size_t bytes)
{
    uint8_t *p = (uint8_t *)ptr;
    while (bytes-- != 0u)
        *p++ = 0u;
}

#ifdef __cplusplus
}
#endif

#endif
