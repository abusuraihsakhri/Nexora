#include <ai/tensor.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

static ai_tensor *registry[AI_MAX_TENSORS];
static u64 count = 0;
static u64 next_id = 1;

static u64 dtype_size(ai_dtype dtype) {
    switch (dtype) {
        case AI_DTYPE_F32:  return 4;
        case AI_DTYPE_F16:  return 2;
        case AI_DTYPE_BF16: return 2;
        case AI_DTYPE_I8:   return 1;
        case AI_DTYPE_I32:  return 4;
        default:            return 0;
    }
}

void ai_tensor_system_init(void) {
    count = 0;
    next_id = 1;
}

ai_tensor *ai_tensor_create(
    const char *name,
    ai_dtype dtype,
    u32 ndim,
    const u64 *shape,
    ai_tensor_location location,
    u32 flags
) {
    if (count >= AI_MAX_TENSORS) {
        panic("tensor registry full");
    }
    if (ndim == 0 || ndim > AI_MAX_DIMS) {
        panic("invalid tensor rank");
    }

    ai_tensor *t = (ai_tensor *)kalloc(sizeof(ai_tensor), 16);
    t->id = next_id++;
    t->name = name;
    t->dtype = dtype;
    t->ndim = ndim;
    t->location = location;
    t->flags = flags;
    t->reuse_hint = 0;

    u64 elements = 1;
    for (u32 i = 0; i < AI_MAX_DIMS; ++i) {
        t->shape[i] = 0;
    }
    for (u32 i = 0; i < ndim; ++i) {
        t->shape[i] = shape[i];
        elements *= shape[i];
    }

    t->bytes = elements * dtype_size(dtype);
    registry[count++] = t;
    return t;
}

u64 ai_tensor_count(void) {
    return count;
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
