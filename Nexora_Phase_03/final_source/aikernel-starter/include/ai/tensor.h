#ifndef AIKERNEL_AI_TENSOR_H
#define AIKERNEL_AI_TENSOR_H

#include <kernel/types.h>
#include <kernel/object.h>
#include <kernel/memory_object.h>
#include <ai/lifetime.h>
#include <ai/device.h>

#define AI_MAX_DIMS 8
#define AI_MAX_TENSORS 128
#define AI_MAX_TENSOR_CONSUMERS 32

typedef enum {
    AI_DTYPE_F32,
    AI_DTYPE_F16,
    AI_DTYPE_BF16,
    AI_DTYPE_I8,
    AI_DTYPE_I32,
    AI_DTYPE_UNKNOWN
} ai_dtype;

typedef enum {
    AI_TENSOR_CLASS_GENERIC = 0,
    AI_TENSOR_CLASS_INPUT,
    AI_TENSOR_CLASS_OUTPUT,
    AI_TENSOR_CLASS_WEIGHT,
    AI_TENSOR_CLASS_ACTIVATION,
    AI_TENSOR_CLASS_KV_CACHE,
    AI_TENSOR_CLASS_GRADIENT,
    AI_TENSOR_CLASS_SCRATCH,
    AI_TENSOR_CLASS_COUNT
} ai_tensor_class;

typedef enum {
    AI_TENSOR_LAYOUT_CONTIGUOUS = 0,
    AI_TENSOR_LAYOUT_STRIDED
} ai_tensor_layout;

typedef enum {
    AI_LOC_CPU_RAM,
    AI_LOC_GPU_HBM,
    AI_LOC_NPU_MEM,
    AI_LOC_NVME,
    AI_LOC_REMOTE,
    AI_LOC_COUNT
} ai_tensor_location;

typedef enum {
    AI_TENSOR_PERSISTENT = 1u << 0,
    AI_TENSOR_EPHEMERAL  = 1u << 1,
    AI_TENSOR_READONLY   = 1u << 2,
    AI_TENSOR_PINNED     = 1u << 3,
    AI_TENSOR_CONTIGUOUS = 1u << 4,
    AI_TENSOR_VIEW       = 1u << 5,
    AI_TENSOR_EXTERNAL   = 1u << 6,
    AI_TENSOR_ZERO_INIT  = 1u << 7
} ai_tensor_flags;

typedef enum {
    AI_TENSOR_OK = 0,
    AI_TENSOR_ERR_INVALID_ARGUMENT,
    AI_TENSOR_ERR_INVALID_DTYPE,
    AI_TENSOR_ERR_INVALID_CLASS,
    AI_TENSOR_ERR_INVALID_LOCATION,
    AI_TENSOR_ERR_INVALID_RANK,
    AI_TENSOR_ERR_ZERO_EXTENT,
    AI_TENSOR_ERR_SHAPE_OVERFLOW,
    AI_TENSOR_ERR_BYTE_SIZE_OVERFLOW,
    AI_TENSOR_ERR_INVALID_STRIDE,
    AI_TENSOR_ERR_INVALID_FLAGS,
    AI_TENSOR_ERR_INVALID_LIFETIME,
    AI_TENSOR_ERR_REGISTRY_FULL,
    AI_TENSOR_ERR_OBJECT_REGISTRY_FULL,
    AI_TENSOR_ERR_BACKING_ALREADY_BOUND,
    AI_TENSOR_ERR_BACKING_NOT_FOUND,
    AI_TENSOR_ERR_BACKING_TOO_SMALL,
    AI_TENSOR_ERR_BACKING_ALLOC_FAILED,
    AI_TENSOR_ERR_VIEW_REQUIRES_EXISTING_BACKING,
    AI_TENSOR_ERR_EXTERNAL_REQUIRES_EXISTING_BACKING,
    AI_TENSOR_ERR_INVALID_DEVICE
} ai_tensor_status;

typedef struct {
    const char *name;
    ai_dtype dtype;
    ai_tensor_class tensor_class;
    u32 ndim;
    const u64 *shape;

    /*
     * Byte strides. NULL requests canonical row-major contiguous strides.
     * Explicit strides permit views and other non-contiguous layouts.
     */
    const u64 *stride_bytes;

    ai_tensor_location location;
    ai_tensor_lifetime lifetime;
    u32 flags;
} ai_tensor_desc;

typedef struct {
    u64 created;
    u64 destroyed;
    u64 live;
    u64 logical_bytes;
    u64 peak_logical_bytes;
    u64 backing_refs_acquired;
    u64 backing_refs_released;
} ai_tensor_stats;

typedef struct ai_tensor {
    nx_object object;

    ai_dtype dtype;
    ai_tensor_class tensor_class;
    ai_tensor_layout layout;

    u32 ndim;
    u64 shape[AI_MAX_DIMS];
    u64 stride_bytes[AI_MAX_DIMS];

    u64 element_count;
    u64 logical_bytes;
    u64 storage_span_bytes;

    ai_tensor_location location;
    ai_device_id_t preferred_device;
    ai_device_id_t resident_device;
    ai_tensor_lifetime lifetime;
    ai_tensor_residency residency;
    ai_reclaim_reason last_reclaim_reason;
    u64 reclaim_count;
    u32 flags;
    u32 reuse_hint;

    /*
     * Phase 3 Step 6: kernel-visible dataflow provenance.
     * These are deliberately non-owning work-object handles. Work nodes retain
     * tensors, not the other way around, which prevents tensor<->work cycles.
     */
    nx_handle_t producer_work;
    nx_handle_t consumers[AI_MAX_TENSOR_CONSUMERS];
    u64 consumer_done_mask;
    u32 consumer_count;
    u32 consumers_remaining;

    /*
     * Backing is referenced by a generation-checked memory-object handle.
     * Step 4 makes this a strong reference: a bound tensor keeps its backing
     * alive until the tensor unbinds or is destroyed.
     */
    nx_handle_t backing_memory;
    u64 backing_offset;
    u64 backing_capacity;
} ai_tensor;

void ai_tensor_system_init(void);

ai_tensor_status ai_tensor_try_create(
    const ai_tensor_desc *desc,
    ai_tensor **out_tensor
);

ai_tensor_status ai_tensor_try_create_owned(
    const ai_tensor_desc *desc,
    nx_owner_id_t owner_id,
    ai_tensor **out_tensor
);

ai_tensor *ai_tensor_create(
    const char *name,
    ai_dtype dtype,
    ai_tensor_class tensor_class,
    u32 ndim,
    const u64 *shape,
    ai_tensor_location location,
    u32 flags
);



ai_tensor *ai_tensor_create_with_lifetime(
    const char *name,
    ai_dtype dtype,
    ai_tensor_class tensor_class,
    u32 ndim,
    const u64 *shape,
    ai_tensor_location location,
    ai_tensor_lifetime lifetime,
    u32 flags
);

ai_tensor *ai_tensor_create_owned_with_lifetime(
    const char *name,
    ai_dtype dtype,
    ai_tensor_class tensor_class,
    u32 ndim,
    const u64 *shape,
    ai_tensor_location location,
    ai_tensor_lifetime lifetime,
    u32 flags,
    nx_owner_id_t owner_id
);
ai_tensor *ai_tensor_create_owned(
    const char *name,
    ai_dtype dtype,
    ai_tensor_class tensor_class,
    u32 ndim,
    const u64 *shape,
    ai_tensor_location location,
    u32 flags,
    nx_owner_id_t owner_id
);

bool ai_tensor_retain(ai_tensor *tensor);
bool ai_tensor_release(ai_tensor *tensor);
bool ai_tensor_pin(ai_tensor *tensor);
bool ai_tensor_unpin(ai_tensor *tensor);

u64 ai_tensor_count(void);
ai_tensor *ai_tensor_at(u64 index);
ai_tensor *ai_tensor_lookup(nx_handle_t handle);
const ai_tensor_stats *ai_tensor_get_stats(void);

u64 ai_dtype_size(ai_dtype dtype);
bool ai_tensor_is_contiguous(const ai_tensor *tensor);
bool ai_tensor_is_backed(const ai_tensor *tensor);

ai_tensor_status ai_tensor_set_preferred_device(
    ai_tensor *tensor,
    ai_device_id_t device_id
);

ai_tensor_status ai_tensor_set_resident_device(
    ai_tensor *tensor,
    ai_device_id_t device_id
);

ai_device_mask ai_tensor_required_device_kind(const ai_tensor *tensor);

ai_tensor_status ai_tensor_bind_memory(
    ai_tensor *tensor,
    nx_handle_t memory_handle,
    u64 offset
);

ai_tensor_status ai_tensor_allocate_backing(
    ai_tensor *tensor,
    u64 alignment
);

bool ai_tensor_unbind_memory(ai_tensor *tensor);
nx_memory *ai_tensor_backing(const ai_tensor *tensor);
void *ai_tensor_data(ai_tensor *tensor);

const char *ai_dtype_name(ai_dtype dtype);
const char *ai_tensor_class_name(ai_tensor_class tensor_class);
const char *ai_tensor_layout_name(ai_tensor_layout layout);
const char *ai_location_name(ai_tensor_location location);
const char *ai_tensor_status_name(ai_tensor_status status);

#endif
