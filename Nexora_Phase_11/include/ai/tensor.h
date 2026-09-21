#ifndef AIKERNEL_AI_TENSOR_H
#define AIKERNEL_AI_TENSOR_H

#include <kernel/types.h>

#define AI_MAX_DIMS 8
#define AI_MAX_TENSORS 128

typedef enum {
    AI_DTYPE_F32,
    AI_DTYPE_F16,
    AI_DTYPE_BF16,
    AI_DTYPE_I8,
    AI_DTYPE_I32,
    AI_DTYPE_UNKNOWN
} ai_dtype;

typedef enum {
    AI_LOC_CPU_RAM,
    AI_LOC_GPU_HBM,
    AI_LOC_NPU_MEM,
    AI_LOC_NVME,
    AI_LOC_REMOTE
} ai_tensor_location;

typedef enum {
    AI_TENSOR_PERSISTENT = 1u << 0,
    AI_TENSOR_EPHEMERAL  = 1u << 1,
    AI_TENSOR_READONLY   = 1u << 2,
    AI_TENSOR_PINNED     = 1u << 3
} ai_tensor_flags;

typedef struct {
    u64 id;
    const char *name;
    ai_dtype dtype;
    u32 ndim;
    u64 shape[AI_MAX_DIMS];
    u64 bytes;
    ai_tensor_location location;
    u32 flags;
    u32 reuse_hint;
} ai_tensor;

void ai_tensor_system_init(void);
ai_tensor *ai_tensor_create(
    const char *name,
    ai_dtype dtype,
    u32 ndim,
    const u64 *shape,
    ai_tensor_location location,
    u32 flags
);
u64 ai_tensor_count(void);
const char *ai_dtype_name(ai_dtype dtype);
const char *ai_location_name(ai_tensor_location location);

#endif
