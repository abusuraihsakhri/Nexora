#include <ai/tensor.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <ai/instrument.h>

static ai_tensor *registry[AI_MAX_TENSORS];
static u64 count = 0;
static ai_tensor_stats stats;

static bool add_u64_checked(u64 a, u64 b, u64 *out) {
    const u64 max = ~(u64)0;
    if (a > max - b) {
        return false;
    }
    *out = a + b;
    return true;
}

static bool mul_u64_checked(u64 a, u64 b, u64 *out) {
    const u64 max = ~(u64)0;

    if (a == 0 || b == 0) {
        *out = 0;
        return true;
    }

    if (a > max / b) {
        return false;
    }

    *out = a * b;
    return true;
}

u64 ai_dtype_size(ai_dtype dtype) {
    switch (dtype) {
        case AI_DTYPE_F32:  return 4;
        case AI_DTYPE_F16:  return 2;
        case AI_DTYPE_BF16: return 2;
        case AI_DTYPE_I8:   return 1;
        case AI_DTYPE_I32:  return 4;
        default:            return 0;
    }
}

static ai_tensor_lifetime infer_lifetime_from_flags(u32 flags) {
    if (flags & AI_TENSOR_EXTERNAL) {
        return AI_TENSOR_LIFETIME_EXTERNAL;
    }
    if (flags & AI_TENSOR_PERSISTENT) {
        return AI_TENSOR_LIFETIME_PERSISTENT;
    }
    return AI_TENSOR_LIFETIME_TEMPORARY;
}

static ai_tensor_status normalize_lifetime(
    ai_tensor_lifetime requested,
    u32 *flags,
    ai_tensor_lifetime *normalized
) {
    if (flags == NULL || normalized == NULL || requested >= AI_TENSOR_LIFETIME_COUNT) {
        return AI_TENSOR_ERR_INVALID_LIFETIME;
    }

    ai_tensor_lifetime lifetime = requested;
    if (lifetime == AI_TENSOR_LIFETIME_AUTO) {
        lifetime = infer_lifetime_from_flags(*flags);
    }

    if (lifetime <= AI_TENSOR_LIFETIME_AUTO || lifetime >= AI_TENSOR_LIFETIME_COUNT) {
        return AI_TENSOR_ERR_INVALID_LIFETIME;
    }

    if ((*flags & AI_TENSOR_PERSISTENT) && lifetime != AI_TENSOR_LIFETIME_PERSISTENT) {
        return AI_TENSOR_ERR_INVALID_LIFETIME;
    }
    if ((*flags & AI_TENSOR_EPHEMERAL) && lifetime == AI_TENSOR_LIFETIME_PERSISTENT) {
        return AI_TENSOR_ERR_INVALID_LIFETIME;
    }
    if ((*flags & AI_TENSOR_EXTERNAL) && lifetime != AI_TENSOR_LIFETIME_EXTERNAL) {
        return AI_TENSOR_ERR_INVALID_LIFETIME;
    }

    if (lifetime == AI_TENSOR_LIFETIME_PERSISTENT) {
        *flags |= AI_TENSOR_PERSISTENT;
        *flags &= ~AI_TENSOR_EPHEMERAL;
    } else if (lifetime == AI_TENSOR_LIFETIME_TEMPORARY) {
        *flags |= AI_TENSOR_EPHEMERAL;
        *flags &= ~AI_TENSOR_PERSISTENT;
    } else {
        *flags &= ~(AI_TENSOR_PERSISTENT | AI_TENSOR_EPHEMERAL);
    }

    if (lifetime == AI_TENSOR_LIFETIME_EXTERNAL) {
        *flags |= AI_TENSOR_EXTERNAL;
    }

    *normalized = lifetime;
    return AI_TENSOR_OK;
}

static ai_tensor_status validate_flags(u32 flags) {
    if ((flags & AI_TENSOR_PERSISTENT) && (flags & AI_TENSOR_EPHEMERAL)) {
        return AI_TENSOR_ERR_INVALID_FLAGS;
    }

    if ((flags & AI_TENSOR_VIEW) && (flags & AI_TENSOR_ZERO_INIT)) {
        return AI_TENSOR_ERR_INVALID_FLAGS;
    }

    if ((flags & AI_TENSOR_EXTERNAL) && (flags & AI_TENSOR_ZERO_INIT)) {
        return AI_TENSOR_ERR_INVALID_FLAGS;
    }

    return AI_TENSOR_OK;
}

static ai_device_mask location_device_kind(ai_tensor_location location) {
    switch (location) {
        case AI_LOC_CPU_RAM: return AI_DEVICE_CPU;
        case AI_LOC_GPU_HBM: return AI_DEVICE_GPU;
        case AI_LOC_NPU_MEM: return AI_DEVICE_NPU;
        case AI_LOC_NVME: return AI_DEVICE_STORAGE;
        case AI_LOC_REMOTE: return AI_DEVICE_NIC;
        case AI_LOC_COUNT: break;
        default: break;
    }
    return (ai_device_mask)0;
}

static ai_device_id_t default_device_for_location(ai_tensor_location location) {
    const ai_device_mask kind = location_device_kind(location);
    ai_device *device = kind != 0 ? ai_device_default(kind) : NULL;
    return device != NULL ? device->object.handle : AI_DEVICE_INVALID;
}

static ai_tensor_status compute_shape_and_size(
    const ai_tensor_desc *desc,
    u64 dtype_bytes,
    u64 *element_count,
    u64 *logical_bytes
) {
    u64 elements = 1;

    for (u32 i = 0; i < desc->ndim; ++i) {
        if (desc->shape[i] == 0) {
            return AI_TENSOR_ERR_ZERO_EXTENT;
        }

        if (!mul_u64_checked(elements, desc->shape[i], &elements)) {
            return AI_TENSOR_ERR_SHAPE_OVERFLOW;
        }
    }

    u64 bytes = 0;
    if (!mul_u64_checked(elements, dtype_bytes, &bytes)) {
        return AI_TENSOR_ERR_BYTE_SIZE_OVERFLOW;
    }

    *element_count = elements;
    *logical_bytes = bytes;
    return AI_TENSOR_OK;
}

static ai_tensor_status compute_layout(
    const ai_tensor_desc *desc,
    u64 dtype_bytes,
    u64 *out_strides,
    ai_tensor_layout *out_layout,
    u64 *out_storage_span,
    bool *out_contiguous
) {
    u64 expected_stride = dtype_bytes;
    bool contiguous = true;

    if (desc->stride_bytes == NULL) {
        for (u32 i = desc->ndim; i > 0; --i) {
            const u32 dim = i - 1;
            out_strides[dim] = expected_stride;

            if (!mul_u64_checked(expected_stride, desc->shape[dim], &expected_stride)) {
                return AI_TENSOR_ERR_BYTE_SIZE_OVERFLOW;
            }
        }

        *out_layout = AI_TENSOR_LAYOUT_CONTIGUOUS;
        *out_storage_span = expected_stride;
        *out_contiguous = true;
        return AI_TENSOR_OK;
    }

    /*
     * Validate explicit byte strides and determine whether they are actually
     * canonical contiguous row-major strides.
     */
    expected_stride = dtype_bytes;
    for (u32 i = desc->ndim; i > 0; --i) {
        const u32 dim = i - 1;
        const u64 stride = desc->stride_bytes[dim];

        if (desc->shape[dim] > 1 && stride == 0) {
            return AI_TENSOR_ERR_INVALID_STRIDE;
        }

        if ((stride % dtype_bytes) != 0) {
            return AI_TENSOR_ERR_INVALID_STRIDE;
        }

        out_strides[dim] = stride;
        if (stride != expected_stride) {
            contiguous = false;
        }

        if (!mul_u64_checked(expected_stride, desc->shape[dim], &expected_stride)) {
            return AI_TENSOR_ERR_BYTE_SIZE_OVERFLOW;
        }
    }

    u64 max_offset = 0;
    for (u32 i = 0; i < desc->ndim; ++i) {
        u64 contribution = 0;
        if (!mul_u64_checked(desc->shape[i] - 1, out_strides[i], &contribution)) {
            return AI_TENSOR_ERR_BYTE_SIZE_OVERFLOW;
        }
        if (!add_u64_checked(max_offset, contribution, &max_offset)) {
            return AI_TENSOR_ERR_BYTE_SIZE_OVERFLOW;
        }
    }

    u64 storage_span = 0;
    if (!add_u64_checked(max_offset, dtype_bytes, &storage_span)) {
        return AI_TENSOR_ERR_BYTE_SIZE_OVERFLOW;
    }

    *out_layout = contiguous ? AI_TENSOR_LAYOUT_CONTIGUOUS : AI_TENSOR_LAYOUT_STRIDED;
    *out_storage_span = storage_span;
    *out_contiguous = contiguous;
    return AI_TENSOR_OK;
}

static void ai_tensor_object_destroy(nx_object *object) {
    ai_tensor *tensor = (ai_tensor *)object;
    ai_trace_record(AI_TRACE_TENSOR_DESTROY, tensor->object.id, 0, tensor->logical_bytes);

    /* A tensor owns one strong reference to any bound memory object. */
    if (tensor->backing_memory != NX_INVALID_HANDLE) {
        (void)ai_tensor_unbind_memory(tensor);
    }

    for (u64 i = 0; i < count; ++i) {
        if (registry[i] == tensor) {
            for (u64 j = i + 1; j < count; ++j) {
                registry[j - 1] = registry[j];
            }
            registry[count - 1] = NULL;
            --count;
            break;
        }
    }

    ai_lifetime_track_destroy(tensor);

    ++stats.destroyed;
    if (stats.live > 0) {
        --stats.live;
    }
    if (stats.logical_bytes >= tensor->logical_bytes) {
        stats.logical_bytes -= tensor->logical_bytes;
    } else {
        stats.logical_bytes = 0;
    }
}

void ai_tensor_system_init(void) {
    ai_lifetime_system_init();
    count = 0;
    stats.created = 0;
    stats.destroyed = 0;
    stats.live = 0;
    stats.logical_bytes = 0;
    stats.peak_logical_bytes = 0;
    stats.backing_refs_acquired = 0;
    stats.backing_refs_released = 0;

    for (u32 i = 0; i < AI_MAX_TENSORS; ++i) {
        registry[i] = NULL;
    }
}

ai_tensor_status ai_tensor_try_create(
    const ai_tensor_desc *desc,
    ai_tensor **out_tensor
) {
    return ai_tensor_try_create_owned(desc, NX_OWNER_KERNEL, out_tensor);
}

ai_tensor_status ai_tensor_try_create_owned(
    const ai_tensor_desc *desc,
    nx_owner_id_t owner_id,
    ai_tensor **out_tensor
) {
    if (out_tensor != NULL) {
        *out_tensor = NULL;
    }

    if (desc == NULL || out_tensor == NULL || desc->shape == NULL || desc->name == NULL ||
        owner_id == NX_OWNER_NONE) {
        return AI_TENSOR_ERR_INVALID_ARGUMENT;
    }

    if (count >= AI_MAX_TENSORS) {
        return AI_TENSOR_ERR_REGISTRY_FULL;
    }

    if (desc->ndim == 0 || desc->ndim > AI_MAX_DIMS) {
        return AI_TENSOR_ERR_INVALID_RANK;
    }

    const u64 dtype_bytes = ai_dtype_size(desc->dtype);
    if (dtype_bytes == 0) {
        return AI_TENSOR_ERR_INVALID_DTYPE;
    }

    if (desc->tensor_class < AI_TENSOR_CLASS_GENERIC ||
        desc->tensor_class >= AI_TENSOR_CLASS_COUNT) {
        return AI_TENSOR_ERR_INVALID_CLASS;
    }

    if (desc->location < AI_LOC_CPU_RAM || desc->location >= AI_LOC_COUNT) {
        return AI_TENSOR_ERR_INVALID_LOCATION;
    }

    ai_tensor_status status = validate_flags(desc->flags);
    if (status != AI_TENSOR_OK) {
        return status;
    }

    u32 normalized_flags = desc->flags;
    ai_tensor_lifetime lifetime = AI_TENSOR_LIFETIME_AUTO;
    status = normalize_lifetime(desc->lifetime, &normalized_flags, &lifetime);
    if (status != AI_TENSOR_OK) {
        return status;
    }

    u64 element_count = 0;
    u64 logical_bytes = 0;
    status = compute_shape_and_size(desc, dtype_bytes, &element_count, &logical_bytes);
    if (status != AI_TENSOR_OK) {
        return status;
    }

    u64 strides[AI_MAX_DIMS];
    for (u32 i = 0; i < AI_MAX_DIMS; ++i) {
        strides[i] = 0;
    }

    ai_tensor_layout layout = AI_TENSOR_LAYOUT_CONTIGUOUS;
    u64 storage_span = 0;
    bool contiguous = false;
    status = compute_layout(
        desc,
        dtype_bytes,
        strides,
        &layout,
        &storage_span,
        &contiguous
    );
    if (status != AI_TENSOR_OK) {
        return status;
    }

    if (nx_object_count() >= NX_OBJECT_REGISTRY_CAPACITY) {
        return AI_TENSOR_ERR_OBJECT_REGISTRY_FULL;
    }

    ai_tensor *tensor = (ai_tensor *)kalloc(sizeof(ai_tensor), 16);
    if (!nx_object_register_ex(
            &tensor->object,
            NX_OBJECT_TENSOR,
            desc->name,
            (normalized_flags & AI_TENSOR_PERSISTENT) ? NX_OBJECT_FLAG_PERSISTENT : NX_OBJECT_FLAG_NONE,
            owner_id,
            ai_tensor_object_destroy)) {
        return AI_TENSOR_ERR_OBJECT_REGISTRY_FULL;
    }

    tensor->dtype = desc->dtype;
    tensor->tensor_class = desc->tensor_class;
    tensor->layout = layout;
    tensor->ndim = desc->ndim;

    for (u32 i = 0; i < AI_MAX_DIMS; ++i) {
        tensor->shape[i] = 0;
        tensor->stride_bytes[i] = 0;
    }

    for (u32 i = 0; i < desc->ndim; ++i) {
        tensor->shape[i] = desc->shape[i];
        tensor->stride_bytes[i] = strides[i];
    }

    tensor->element_count = element_count;
    tensor->logical_bytes = logical_bytes;
    tensor->storage_span_bytes = storage_span;
    tensor->location = desc->location;
    tensor->preferred_device = default_device_for_location(desc->location);
    tensor->resident_device = AI_DEVICE_INVALID;
    tensor->lifetime = lifetime;
    tensor->residency = AI_TENSOR_RESIDENCY_UNBACKED;
    tensor->last_reclaim_reason = AI_RECLAIM_EXPLICIT;
    tensor->reclaim_count = 0;
    tensor->flags = normalized_flags;
    if (contiguous) {
        tensor->flags |= AI_TENSOR_CONTIGUOUS;
    } else {
        tensor->flags &= ~AI_TENSOR_CONTIGUOUS;
    }

    tensor->reuse_hint = 0;
    tensor->producer_work = NX_INVALID_HANDLE;
    tensor->consumer_done_mask = 0;
    tensor->consumer_count = 0;
    tensor->consumers_remaining = 0;
    for (u32 i = 0; i < AI_MAX_TENSOR_CONSUMERS; ++i) {
        tensor->consumers[i] = NX_INVALID_HANDLE;
    }
    tensor->backing_memory = NX_INVALID_HANDLE;
    tensor->backing_offset = 0;
    tensor->backing_capacity = 0;

    registry[count++] = tensor;
    ai_lifetime_track_create(tensor);

    ++stats.created;
    ++stats.live;

    u64 new_total = 0;
    if (add_u64_checked(stats.logical_bytes, logical_bytes, &new_total)) {
        stats.logical_bytes = new_total;
    } else {
        stats.logical_bytes = ~(u64)0;
    }

    if (stats.logical_bytes > stats.peak_logical_bytes) {
        stats.peak_logical_bytes = stats.logical_bytes;
    }

    ai_trace_record(AI_TRACE_TENSOR_CREATE, tensor->object.id, 0, tensor->logical_bytes);
    *out_tensor = tensor;
    return AI_TENSOR_OK;
}

ai_tensor *ai_tensor_create(
    const char *name,
    ai_dtype dtype,
    ai_tensor_class tensor_class,
    u32 ndim,
    const u64 *shape,
    ai_tensor_location location,
    u32 flags
) {
    return ai_tensor_create_owned(
        name, dtype, tensor_class, ndim, shape, location, flags, NX_OWNER_KERNEL
    );
}

ai_tensor *ai_tensor_create_with_lifetime(
    const char *name,
    ai_dtype dtype,
    ai_tensor_class tensor_class,
    u32 ndim,
    const u64 *shape,
    ai_tensor_location location,
    ai_tensor_lifetime lifetime,
    u32 flags
) {
    return ai_tensor_create_owned_with_lifetime(
        name, dtype, tensor_class, ndim, shape, location, lifetime, flags, NX_OWNER_KERNEL
    );
}

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
) {
    ai_tensor_desc desc;
    desc.name = name;
    desc.dtype = dtype;
    desc.tensor_class = tensor_class;
    desc.ndim = ndim;
    desc.shape = shape;
    desc.stride_bytes = NULL;
    desc.location = location;
    desc.lifetime = lifetime;
    desc.flags = flags;

    ai_tensor *tensor = NULL;
    const ai_tensor_status status = ai_tensor_try_create_owned(&desc, owner_id, &tensor);
    if (status != AI_TENSOR_OK) {
        panic(ai_tensor_status_name(status));
    }
    return tensor;
}

ai_tensor *ai_tensor_create_owned(
    const char *name,
    ai_dtype dtype,
    ai_tensor_class tensor_class,
    u32 ndim,
    const u64 *shape,
    ai_tensor_location location,
    u32 flags,
    nx_owner_id_t owner_id
) {
    ai_tensor_desc desc;
    desc.name = name;
    desc.dtype = dtype;
    desc.tensor_class = tensor_class;
    desc.ndim = ndim;
    desc.shape = shape;
    desc.stride_bytes = NULL;
    desc.location = location;
    desc.lifetime = AI_TENSOR_LIFETIME_AUTO;
    desc.flags = flags;

    ai_tensor *tensor = NULL;
    const ai_tensor_status status = ai_tensor_try_create_owned(&desc, owner_id, &tensor);
    if (status != AI_TENSOR_OK) {
        panic(ai_tensor_status_name(status));
    }

    return tensor;
}

bool ai_tensor_retain(ai_tensor *tensor) {
    return tensor != NULL && tensor->object.type == NX_OBJECT_TENSOR &&
           nx_object_retain(tensor->object.handle);
}

bool ai_tensor_release(ai_tensor *tensor) {
    if (tensor == NULL || tensor->object.type != NX_OBJECT_TENSOR ||
        tensor->object.handle == NX_INVALID_HANDLE) {
        return false;
    }

    nx_handle_t handle = tensor->object.handle;
    return nx_object_release(handle);
}

bool ai_tensor_pin(ai_tensor *tensor) {
    return tensor != NULL && tensor->object.type == NX_OBJECT_TENSOR &&
           nx_object_pin(tensor->object.handle);
}

bool ai_tensor_unpin(ai_tensor *tensor) {
    return tensor != NULL && tensor->object.type == NX_OBJECT_TENSOR &&
           tensor->object.handle != NX_INVALID_HANDLE &&
           nx_object_unpin(tensor->object.handle);
}

u64 ai_tensor_count(void) {
    return count;
}

ai_tensor *ai_tensor_at(u64 index) {
    return index < count ? registry[index] : NULL;
}

ai_tensor *ai_tensor_lookup(nx_handle_t handle) {
    nx_object *object = nx_object_lookup(handle, NX_OBJECT_TENSOR);
    return object != NULL ? (ai_tensor *)object : NULL;
}

const ai_tensor_stats *ai_tensor_get_stats(void) {
    return &stats;
}

bool ai_tensor_is_contiguous(const ai_tensor *tensor) {
    if (tensor == NULL) {
        return false;
    }
    return tensor->layout == AI_TENSOR_LAYOUT_CONTIGUOUS;
}

bool ai_tensor_is_backed(const ai_tensor *tensor) {
    if (tensor == NULL) {
        return false;
    }
    return tensor->backing_memory != NX_INVALID_HANDLE;
}


ai_device_mask ai_tensor_required_device_kind(const ai_tensor *tensor) {
    return tensor != NULL ? location_device_kind(tensor->location) : (ai_device_mask)0;
}

ai_tensor_status ai_tensor_set_preferred_device(
    ai_tensor *tensor,
    ai_device_id_t device_id
) {
    if (tensor == NULL) {
        return AI_TENSOR_ERR_INVALID_ARGUMENT;
    }
    if (device_id == AI_DEVICE_INVALID) {
        tensor->preferred_device = AI_DEVICE_INVALID;
        return AI_TENSOR_OK;
    }

    ai_device *device = ai_device_lookup(device_id);
    const ai_device_mask required = ai_tensor_required_device_kind(tensor);
    if (device == NULL || required == 0 || device->kind != required) {
        return AI_TENSOR_ERR_INVALID_DEVICE;
    }
    tensor->preferred_device = device_id;
    return AI_TENSOR_OK;
}

ai_tensor_status ai_tensor_set_resident_device(
    ai_tensor *tensor,
    ai_device_id_t device_id
) {
    if (tensor == NULL) {
        return AI_TENSOR_ERR_INVALID_ARGUMENT;
    }
    if (device_id == AI_DEVICE_INVALID) {
        if (ai_tensor_is_backed(tensor)) {
            return AI_TENSOR_ERR_INVALID_DEVICE;
        }
        tensor->resident_device = AI_DEVICE_INVALID;
        return AI_TENSOR_OK;
    }
    if (!ai_tensor_is_backed(tensor)) {
        return AI_TENSOR_ERR_INVALID_DEVICE;
    }

    ai_device *device = ai_device_lookup(device_id);
    const ai_device_mask required = ai_tensor_required_device_kind(tensor);
    if (device == NULL || required == 0 || device->kind != required) {
        return AI_TENSOR_ERR_INVALID_DEVICE;
    }
    tensor->resident_device = device_id;
    return AI_TENSOR_OK;
}

ai_tensor_status ai_tensor_bind_memory(
    ai_tensor *tensor,
    nx_handle_t memory_handle,
    u64 offset
) {
    if (tensor == NULL || memory_handle == NX_INVALID_HANDLE) {
        return AI_TENSOR_ERR_INVALID_ARGUMENT;
    }

    if (tensor->backing_memory != NX_INVALID_HANDLE) {
        return AI_TENSOR_ERR_BACKING_ALREADY_BOUND;
    }

    nx_memory *memory = nx_memory_lookup(memory_handle);
    if (memory == NULL) {
        return AI_TENSOR_ERR_BACKING_NOT_FOUND;
    }

    if (!nx_memory_contains_range(memory, offset, tensor->storage_span_bytes)) {
        return AI_TENSOR_ERR_BACKING_TOO_SMALL;
    }

    if (!nx_memory_retain(memory_handle)) {
        return AI_TENSOR_ERR_BACKING_NOT_FOUND;
    }

    if ((tensor->flags & AI_TENSOR_PINNED) && !nx_memory_pin(memory_handle)) {
        (void)nx_memory_release(memory_handle);
        return AI_TENSOR_ERR_BACKING_NOT_FOUND;
    }

    tensor->backing_memory = memory_handle;
    tensor->backing_offset = offset;
    tensor->backing_capacity = memory->capacity_bytes - offset;
    tensor->residency = AI_TENSOR_RESIDENCY_RESIDENT;
    if (tensor->resident_device == AI_DEVICE_INVALID) {
        tensor->resident_device = tensor->preferred_device != AI_DEVICE_INVALID
            ? tensor->preferred_device
            : default_device_for_location(tensor->location);
    }
    ++stats.backing_refs_acquired;
    ai_trace_record(AI_TRACE_TENSOR_BIND, tensor->object.id, memory->object.id, tensor->storage_span_bytes);
    return AI_TENSOR_OK;
}

ai_tensor_status ai_tensor_allocate_backing(
    ai_tensor *tensor,
    u64 alignment
) {
    if (tensor == NULL) {
        return AI_TENSOR_ERR_INVALID_ARGUMENT;
    }

    if (tensor->backing_memory != NX_INVALID_HANDLE) {
        return AI_TENSOR_ERR_BACKING_ALREADY_BOUND;
    }

    if (tensor->flags & AI_TENSOR_VIEW) {
        return AI_TENSOR_ERR_VIEW_REQUIRES_EXISTING_BACKING;
    }

    if (tensor->flags & AI_TENSOR_EXTERNAL) {
        return AI_TENSOR_ERR_EXTERNAL_REQUIRES_EXISTING_BACKING;
    }

    u32 memory_flags = NX_MEMORY_FLAG_NONE;
    if (tensor->flags & AI_TENSOR_ZERO_INIT) {
        memory_flags |= NX_MEMORY_FLAG_ZERO_INIT;
    }
    if (tensor->flags & AI_TENSOR_READONLY) {
        memory_flags |= NX_MEMORY_FLAG_READONLY;
    }
    if (tensor->flags & AI_TENSOR_PINNED) {
        memory_flags |= NX_MEMORY_FLAG_PINNED;
    }
    if (tensor->location != AI_LOC_CPU_RAM) {
        /*
         * Until a device allocator exists, non-CPU placement is represented
         * by real CPU-resident backing plus an explicit emulation marker.
         */
        memory_flags |= NX_MEMORY_FLAG_EMULATED_DEVICE;
    }

    nx_memory_desc desc;
    desc.name = tensor->object.name;
    desc.size_bytes = tensor->storage_span_bytes;
    desc.alignment = alignment;
    desc.flags = memory_flags;

    nx_memory *memory = NULL;
    const nx_memory_status memory_status = nx_memory_try_create_owned(
        &desc, tensor->object.owner_id, &memory
    );
    if (memory_status != NX_MEMORY_OK || memory == NULL) {
        return AI_TENSOR_ERR_BACKING_ALLOC_FAILED;
    }

    nx_handle_t memory_handle = memory->object.handle;
    ai_tensor_status bind_status = ai_tensor_bind_memory(tensor, memory_handle, 0);

    /* Drop the allocator/creator reference. The tensor now owns the backing. */
    if (!nx_memory_release(memory_handle)) {
        if (bind_status == AI_TENSOR_OK) {
            (void)ai_tensor_unbind_memory(tensor);
        }
        return AI_TENSOR_ERR_BACKING_ALLOC_FAILED;
    }

    return bind_status;
}

bool ai_tensor_unbind_memory(ai_tensor *tensor) {
    if (tensor == NULL || tensor->backing_memory == NX_INVALID_HANDLE) {
        return false;
    }

    nx_handle_t memory_handle = tensor->backing_memory;
    nx_memory *memory_before = nx_memory_lookup(memory_handle);
    const u64 memory_id = memory_before != NULL ? memory_before->object.id : 0;
    bool was_pinned = (tensor->flags & AI_TENSOR_PINNED) != 0;

    if (was_pinned && !nx_memory_unpin(memory_handle)) {
        return false;
    }

    if (!nx_memory_release(memory_handle)) {
        /* Preserve the pre-call lifecycle contract if release unexpectedly fails. */
        if (was_pinned) {
            (void)nx_memory_pin(memory_handle);
        }
        return false;
    }

    tensor->backing_memory = NX_INVALID_HANDLE;
    tensor->backing_offset = 0;
    tensor->backing_capacity = 0;
    tensor->residency = AI_TENSOR_RESIDENCY_UNBACKED;
    tensor->resident_device = AI_DEVICE_INVALID;
    ++stats.backing_refs_released;
    ai_trace_record(AI_TRACE_TENSOR_UNBIND, tensor->object.id, memory_id, tensor->storage_span_bytes);
    return true;
}

nx_memory *ai_tensor_backing(const ai_tensor *tensor) {
    if (tensor == NULL || tensor->backing_memory == NX_INVALID_HANDLE) {
        return NULL;
    }
    return nx_memory_lookup(tensor->backing_memory);
}

void *ai_tensor_data(ai_tensor *tensor) {
    nx_memory *memory = ai_tensor_backing(tensor);
    if (memory == NULL) {
        return NULL;
    }

    return nx_memory_ptr(memory, tensor->backing_offset, tensor->storage_span_bytes);
}

const char *ai_dtype_name(ai_dtype dtype) {
    switch (dtype) {
        case AI_DTYPE_F32: return "f32";
        case AI_DTYPE_F16: return "f16";
        case AI_DTYPE_BF16: return "bf16";
        case AI_DTYPE_I8: return "i8";
        case AI_DTYPE_I32: return "i32";
        default: return "unknown";
    }
}

const char *ai_tensor_class_name(ai_tensor_class tensor_class) {
    switch (tensor_class) {
        case AI_TENSOR_CLASS_GENERIC: return "generic";
        case AI_TENSOR_CLASS_INPUT: return "input";
        case AI_TENSOR_CLASS_OUTPUT: return "output";
        case AI_TENSOR_CLASS_WEIGHT: return "weight";
        case AI_TENSOR_CLASS_ACTIVATION: return "activation";
        case AI_TENSOR_CLASS_KV_CACHE: return "kv-cache";
        case AI_TENSOR_CLASS_GRADIENT: return "gradient";
        case AI_TENSOR_CLASS_SCRATCH: return "scratch";
        default: return "unknown";
    }
}

const char *ai_tensor_layout_name(ai_tensor_layout layout) {
    switch (layout) {
        case AI_TENSOR_LAYOUT_CONTIGUOUS: return "contiguous";
        case AI_TENSOR_LAYOUT_STRIDED: return "strided";
        default: return "unknown";
    }
}

const char *ai_location_name(ai_tensor_location location) {
    switch (location) {
        case AI_LOC_CPU_RAM: return "CPU_RAM";
        case AI_LOC_GPU_HBM: return "GPU_HBM";
        case AI_LOC_NPU_MEM: return "NPU_MEM";
        case AI_LOC_NVME: return "NVME";
        case AI_LOC_REMOTE: return "REMOTE";
        default: return "UNKNOWN";
    }
}

const char *ai_tensor_status_name(ai_tensor_status status) {
    switch (status) {
        case AI_TENSOR_OK: return "tensor ok";
        case AI_TENSOR_ERR_INVALID_ARGUMENT: return "tensor invalid argument";
        case AI_TENSOR_ERR_INVALID_DTYPE: return "tensor invalid dtype";
        case AI_TENSOR_ERR_INVALID_CLASS: return "tensor invalid class";
        case AI_TENSOR_ERR_INVALID_LOCATION: return "tensor invalid location";
        case AI_TENSOR_ERR_INVALID_RANK: return "tensor invalid rank";
        case AI_TENSOR_ERR_ZERO_EXTENT: return "tensor zero extent";
        case AI_TENSOR_ERR_SHAPE_OVERFLOW: return "tensor shape overflow";
        case AI_TENSOR_ERR_BYTE_SIZE_OVERFLOW: return "tensor byte size overflow";
        case AI_TENSOR_ERR_INVALID_STRIDE: return "tensor invalid stride";
        case AI_TENSOR_ERR_INVALID_FLAGS: return "tensor invalid flags";
        case AI_TENSOR_ERR_INVALID_LIFETIME: return "tensor invalid lifetime";
        case AI_TENSOR_ERR_REGISTRY_FULL: return "tensor registry full";
        case AI_TENSOR_ERR_OBJECT_REGISTRY_FULL: return "object registry full";
        case AI_TENSOR_ERR_BACKING_ALREADY_BOUND: return "tensor backing already bound";
        case AI_TENSOR_ERR_BACKING_NOT_FOUND: return "tensor backing not found";
        case AI_TENSOR_ERR_BACKING_TOO_SMALL: return "tensor backing too small";
        case AI_TENSOR_ERR_BACKING_ALLOC_FAILED: return "tensor backing allocation failed";
        case AI_TENSOR_ERR_VIEW_REQUIRES_EXISTING_BACKING: return "tensor view requires existing backing";
        case AI_TENSOR_ERR_EXTERNAL_REQUIRES_EXISTING_BACKING: return "external tensor requires existing backing";
        case AI_TENSOR_ERR_INVALID_DEVICE: return "tensor invalid device";
        default: return "tensor unknown error";
    }
}
