#include <ai/tensor.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

static ai_tensor *registry[AI_MAX_TENSORS];
static u64 count = 0;
static u64 next_id = 1;
static u64 total_bytes = 0;
static u64 bytes_by_location[AI_LOC_REMOTE + 1];

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

static bool multiply_u64_checked(u64 a, u64 b, u64 *out) {
    if (!out) return false;
    if (a == 0 || b == 0) {
        *out = 0;
        return true;
    }
    if (a > (~0ull) / b) return false;
    *out = a * b;
    return true;
}

void ai_tensor_system_init(void) {
    count = 0;
    next_id = 1;
    total_bytes = 0;
    for (u32 i = 0; i <= (u32)AI_LOC_REMOTE; ++i) {
        bytes_by_location[i] = 0;
    }
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
    if (!name || !shape) {
        panic("tensor name/shape must not be null");
    }
    if (ndim == 0 || ndim > AI_MAX_DIMS) {
        panic("invalid tensor rank");
    }
    if (dtype_size(dtype) == 0) {
        panic("invalid tensor dtype");
    }
    if ((u32)location > (u32)AI_LOC_REMOTE) {
        panic("invalid tensor location");
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
        if (shape[i] == 0) {
            panic("tensor dimensions must be non-zero");
        }
        t->shape[i] = shape[i];
        if (!multiply_u64_checked(elements, shape[i], &elements)) {
            panic("tensor element count overflow");
        }
    }

    if (!multiply_u64_checked(elements, dtype_size(dtype), &t->bytes)) {
        panic("tensor byte size overflow");
    }
    if (total_bytes > (~0ull) - t->bytes) {
        panic("tensor byte accounting overflow");
    }

    total_bytes += t->bytes;
    bytes_by_location[(u32)location] += t->bytes;
    registry[count++] = t;
    return t;
}

u64 ai_tensor_count(void) {
    return count;
}

u64 ai_tensor_total_bytes(void) {
    return total_bytes;
}

u64 ai_tensor_bytes_at_location(ai_tensor_location location) {
    if ((u32)location > (u32)AI_LOC_REMOTE) return 0;
    return bytes_by_location[(u32)location];
}

bool ai_tensor_validate(const ai_tensor *tensor) {
    if (!tensor || !tensor->name) return false;
    if (tensor->id == 0) return false;
    if (tensor->ndim == 0 || tensor->ndim > AI_MAX_DIMS) return false;
    if (dtype_size(tensor->dtype) == 0) return false;
    if ((u32)tensor->location > (u32)AI_LOC_REMOTE) return false;
    if (tensor->bytes == 0) return false;
    for (u32 i = 0; i < tensor->ndim; ++i) {
        if (tensor->shape[i] == 0) return false;
    }
    return true;
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
