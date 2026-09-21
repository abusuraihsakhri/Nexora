#ifndef AIKERNEL_AI_DEVICE_H
#define AIKERNEL_AI_DEVICE_H

#include <kernel/types.h>
#include <kernel/object.h>

#define AI_MAX_DEVICES 16u
#define AI_MAX_DEVICE_LINKS (AI_MAX_DEVICES * AI_MAX_DEVICES)
#define AI_DEVICE_INVALID ((ai_device_id_t)NX_INVALID_HANDLE)

typedef u64 ai_device_id_t;

typedef enum {
    AI_DEVICE_CPU = 1u << 0,
    AI_DEVICE_GPU = 1u << 1,
    AI_DEVICE_NPU = 1u << 2,
    AI_DEVICE_NIC = 1u << 3,
    AI_DEVICE_STORAGE = 1u << 4
} ai_device_mask;

#define AI_DEVICE_VALID_MASK \
    (AI_DEVICE_CPU | AI_DEVICE_GPU | AI_DEVICE_NPU | AI_DEVICE_NIC | AI_DEVICE_STORAGE)

typedef enum {
    AI_DEVICE_OK = 0,
    AI_DEVICE_ERR_INVALID_ARGUMENT,
    AI_DEVICE_ERR_INVALID_KIND,
    AI_DEVICE_ERR_REGISTRY_FULL,
    AI_DEVICE_ERR_OBJECT_REGISTRY_FULL,
    AI_DEVICE_ERR_NOT_FOUND,
    AI_DEVICE_ERR_LINK_NOT_FOUND,
    AI_DEVICE_ERR_OVERFLOW
} ai_device_status;

typedef struct {
    const char *name;
    ai_device_mask kind;
    u64 memory_bytes;
    u32 numa_node;

    /* Synthetic capability data used by the Step 8 placement model. */
    u64 compute_ops_per_sec;
    u64 memory_bandwidth_bytes_per_sec;
} ai_device_desc;

typedef struct ai_device {
    nx_object object;
    ai_device_mask kind;
    u64 memory_bytes;
    u32 numa_node;
    u64 compute_ops_per_sec;
    u64 memory_bandwidth_bytes_per_sec;
} ai_device;

typedef struct {
    ai_device_id_t from;
    ai_device_id_t to;
    u64 bandwidth_bytes_per_sec;
    u64 latency_ns;
    bool valid;
} ai_device_link;

typedef struct {
    u64 devices_registered;
    u64 links_configured;
    u64 transfer_estimates;
    u64 transfer_estimate_failures;
} ai_device_stats;

/*
 * Installs a small synthetic CPU/GPU/NPU topology. It is a policy/test model,
 * not hardware discovery. Real bus/driver enumeration belongs to later phases.
 */
void ai_device_system_init(void);

ai_device_status ai_device_try_register(
    const ai_device_desc *desc,
    ai_device **out_device
);

ai_device *ai_device_register(const ai_device_desc *desc);
ai_device *ai_device_lookup(ai_device_id_t id);
ai_device *ai_device_default(ai_device_mask kind);
ai_device *ai_device_at(u32 index);
u32 ai_device_count(void);
const ai_device_stats *ai_device_get_stats(void);

bool ai_device_supports_mask(const ai_device *device, u32 allowed_mask);

ai_device_status ai_device_set_link(
    ai_device_id_t from,
    ai_device_id_t to,
    u64 bandwidth_bytes_per_sec,
    u64 latency_ns,
    bool bidirectional
);

ai_device_status ai_device_estimate_transfer_ns(
    ai_device_id_t from,
    ai_device_id_t to,
    u64 bytes,
    u64 *out_ns
);

const char *ai_device_kind_name(ai_device_mask kind);
const char *ai_device_status_name(ai_device_status status);

#endif
