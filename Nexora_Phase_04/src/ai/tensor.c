#include <ai/tensor.h>
#include <kernel/atomic.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>

static ai_tensor *registry[AI_MAX_TENSORS];
static u64 count = 0;
static u64 next_id = 1;
static ai_spinlock registry_lock = AI_SPINLOCK_INITIALIZER;

static u64 dtype_size(ai_dtype dtype) {
    switch (dtype) {
        case AI_DTYPE_F32: return 4;
        case AI_DTYPE_F16: return 2;
        case AI_DTYPE_BF16: return 2;
        case AI_DTYPE_I8: return 1;
        case AI_DTYPE_I32: return 4;
        default: return 0;
    }
}

static void retire_tensor(ai_tensor *tensor) {
    if (!tensor || !tensor->managed) return;

    u32 expected = (u32)AI_TENSOR_LIVE;
    if (!ai_atomic_compare_exchange_u32(
            (volatile u32 *)&tensor->lifetime_state,
            &expected,
            (u32)AI_TENSOR_RETIRED)) {
        return;
    }

    ai_tensor_backing *backing = tensor->backing;
    tensor->backing = NULL;
    tensor->backing_offset = 0;

    ai_spin_lock(&registry_lock);
    for (u32 i = 0; i < AI_MAX_TENSORS; ++i) {
        if (registry[i] == tensor) {
            registry[i] = NULL;
            if (count == 0) {
                ai_spin_unlock(&registry_lock);
                panic("tensor registry underflow");
            }
            count--;
            break;
        }
    }
    ai_spin_unlock(&registry_lock);

    if (backing && !ai_backing_put(backing)) {
        panic("tensor backing reference lost during retirement");
    }
}

void ai_tensor_system_init(void) {
    ai_spinlock_init(&registry_lock);
    ai_spin_lock(&registry_lock);
    for (u32 i = 0; i < AI_MAX_TENSORS; ++i) registry[i] = NULL;
    count = 0;
    next_id = 1;
    ai_spin_unlock(&registry_lock);
}

ai_tensor *ai_tensor_create(
    const char *name,
    ai_dtype dtype,
    u32 ndim,
    const u64 *shape,
    ai_tensor_location location,
    u32 flags
) {
    if (ndim == 0 || ndim > AI_MAX_DIMS || !shape) {
        panic("invalid tensor rank");
    }

    u64 element_size = dtype_size(dtype);
    if (element_size == 0) panic("invalid tensor dtype");

    ai_tensor *t = (ai_tensor *)kalloc(sizeof(ai_tensor), 16);
    t->name = name;
    t->dtype = dtype;
    t->ndim = ndim;
    t->location = location;
    t->flags = flags;
    t->reuse_hint = 0;
    t->backing = NULL;
    t->backing_offset = 0;
    ai_atomic_store_u64(&t->refcount, 1);
    ai_atomic_store_u32(
        (volatile u32 *)&t->lifetime_state,
        (u32)AI_TENSOR_LIVE);
    t->managed = true;

    u64 elements = 1;
    for (u32 i = 0; i < AI_MAX_DIMS; ++i) t->shape[i] = 0;
    for (u32 i = 0; i < ndim; ++i) {
        if (shape[i] == 0 || elements > (~0ull / shape[i])) {
            panic("tensor shape overflow");
        }
        t->shape[i] = shape[i];
        elements *= shape[i];
    }
    if (elements > (~0ull / element_size)) panic("tensor byte-size overflow");
    t->bytes = elements * element_size;

    ai_spin_lock(&registry_lock);
    if (count >= AI_MAX_TENSORS) {
        ai_spin_unlock(&registry_lock);
        panic("tensor registry full");
    }

    t->id = next_id++;
    for (u32 i = 0; i < AI_MAX_TENSORS; ++i) {
        if (registry[i] == NULL) {
            registry[i] = t;
            count++;
            ai_spin_unlock(&registry_lock);
            return t;
        }
    }
    ai_spin_unlock(&registry_lock);
    panic("tensor registry invariant violated");
    return NULL;
}

bool ai_tensor_attach_backing(
    ai_tensor *tensor,
    ai_tensor_backing *backing,
    u64 backing_offset
) {
    if (!tensor || !backing || tensor->backing != NULL) return false;
    if (tensor->managed && !ai_tensor_is_live(tensor)) return false;
    if (!ai_backing_is_live(backing)) return false;
    if (backing_offset > backing->size_bytes ||
        tensor->bytes > backing->size_bytes - backing_offset) {
        return false;
    }
    if (!ai_backing_get(backing)) return false;

    /* Attachment is a pre-publication operation in the current object model. */
    tensor->backing = backing;
    tensor->backing_offset = backing_offset;
    return true;
}

bool ai_tensor_get(ai_tensor *tensor) {
    if (!tensor) return false;
    if (!tensor->managed) return true;
    if (ai_atomic_load_u32((volatile u32 *)&tensor->lifetime_state) !=
        (u32)AI_TENSOR_LIVE) {
        return false;
    }

    u64 current = ai_atomic_load_u64(&tensor->refcount);
    for (;;) {
        if (current == 0 || current == ~0ull) return false;
        u64 expected = current;
        if (ai_atomic_compare_exchange_u64(
                &tensor->refcount, &expected, current + 1ull)) {
            return true;
        }
        current = expected;
    }
}

bool ai_tensor_put(ai_tensor *tensor) {
    if (!tensor) return false;
    if (!tensor->managed) return true;
    if (ai_atomic_load_u32((volatile u32 *)&tensor->lifetime_state) !=
        (u32)AI_TENSOR_LIVE) {
        return false;
    }

    u64 current = ai_atomic_load_u64(&tensor->refcount);
    for (;;) {
        if (current == 0) return false;
        u64 expected = current;
        if (ai_atomic_compare_exchange_u64(
                &tensor->refcount, &expected, current - 1ull)) {
            if (current == 1ull) retire_tensor(tensor);
            return true;
        }
        current = expected;
    }
}

bool ai_tensor_is_live(const ai_tensor *tensor) {
    if (!tensor) return false;
    if (!tensor->managed) return true;
    return ai_atomic_load_u32((const volatile u32 *)&tensor->lifetime_state) ==
               (u32)AI_TENSOR_LIVE &&
           ai_atomic_load_u64(&tensor->refcount) != 0;
}

u64 ai_tensor_refcount(const ai_tensor *tensor) {
    if (!tensor) return 0;
    return tensor->managed ? ai_atomic_load_u64(&tensor->refcount) : 0;
}

u64 ai_tensor_count(void) {
    ai_spin_lock(&registry_lock);
    u64 result = count;
    ai_spin_unlock(&registry_lock);
    return result;
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

const char *ai_tensor_lifetime_state_name(ai_tensor_lifetime_state state) {
    switch (state) {
        case AI_TENSOR_LIVE: return "LIVE";
        case AI_TENSOR_RETIRED: return "RETIRED";
        default: return "UNKNOWN";
    }
}
