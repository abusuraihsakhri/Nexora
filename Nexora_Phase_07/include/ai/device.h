#ifndef AIKERNEL_AI_DEVICE_H
#define AIKERNEL_AI_DEVICE_H

#include <kernel/types.h>

typedef enum {
    AI_DEVICE_CPU = 1u << 0,
    AI_DEVICE_GPU = 1u << 1,
    AI_DEVICE_NPU = 1u << 2,
    AI_DEVICE_NIC = 1u << 3
} ai_device_mask;

typedef struct {
    u32 id;
    ai_device_mask kind;
    const char *name;
    u64 memory_bytes;
    u32 numa_node;
} ai_device;

#endif
