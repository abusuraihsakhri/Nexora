#ifndef AIKERNEL_AI_CAPABILITY_H
#define AIKERNEL_AI_CAPABILITY_H

#include <kernel/types.h>

typedef enum {
    AI_CAP_TENSOR_READ   = 1ull << 0,
    AI_CAP_TENSOR_WRITE  = 1ull << 1,
    AI_CAP_EXECUTE       = 1ull << 2,
    AI_CAP_NETWORK       = 1ull << 3,
    AI_CAP_MODEL_USE     = 1ull << 4,
    AI_CAP_GPU_USE       = 1ull << 5,
    AI_CAP_NPU_USE       = 1ull << 6,
    AI_CAP_ADMIN         = 1ull << 63
} ai_capability_bits;

typedef struct {
    u64 subject_id;
    u64 rights;
} ai_capability;

ai_capability ai_cap_create(u64 subject_id, u64 rights);
bool ai_cap_has(const ai_capability *cap, u64 requested);

#endif
