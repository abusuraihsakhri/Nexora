#include <kernel/memory_object.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

static nx_memory_stats stats;

static bool is_power_of_two(u64 value) {
    return value != 0 && (value & (value - 1u)) == 0;
}

static bool add_u64_checked(u64 a, u64 b, u64 *out) {
    const u64 max = ~(u64)0;
    if (a > max - b) {
        return false;
    }
    *out = a + b;
    return true;
}

static bool align_up_checked(u64 value, u64 alignment, u64 *out) {
    u64 adjusted = 0;
    if (!add_u64_checked(value, alignment - 1u, &adjusted)) {
        return false;
    }
    *out = adjusted & ~(alignment - 1u);
    return true;
}

static void zero_bytes(void *ptr, u64 size) {
    u8 *bytes = (u8 *)ptr;
    for (u64 i = 0; i < size; ++i) {
        bytes[i] = 0;
    }
}

static void nx_memory_object_destroy(nx_object *object) {
    nx_memory *memory = (nx_memory *)object;

    if (stats.live > 0) {
        --stats.live;
    }
    ++stats.destroyed;

    if (stats.requested_bytes >= memory->size_bytes) {
        stats.requested_bytes -= memory->size_bytes;
    } else {
        stats.requested_bytes = 0;
    }

    if (stats.resident_bytes >= memory->capacity_bytes) {
        stats.resident_bytes -= memory->capacity_bytes;
    } else {
        stats.resident_bytes = 0;
    }

    /*
     * The Step 3/4 early heap is monotonic, so these bytes are no longer
     * logically resident but cannot yet be returned to a free-list. Clearing
     * the address prevents accidental reuse through a stale C pointer.
     */
    memory->kernel_address = NULL;
    memory->physical_base = 0;
}

void nx_memory_system_init(void) {
    stats.created = 0;
    stats.destroyed = 0;
    stats.live = 0;
    stats.requested_bytes = 0;
    stats.resident_bytes = 0;
    stats.peak_resident_bytes = 0;
    stats.allocation_failures = 0;
}

nx_memory_status nx_memory_try_create(
    const nx_memory_desc *desc,
    nx_memory **out_memory
) {
    return nx_memory_try_create_owned(desc, NX_OWNER_KERNEL, out_memory);
}

nx_memory_status nx_memory_try_create_owned(
    const nx_memory_desc *desc,
    nx_owner_id_t owner_id,
    nx_memory **out_memory
) {
    if (out_memory != NULL) {
        *out_memory = NULL;
    }

    if (desc == NULL || out_memory == NULL || desc->name == NULL ||
        owner_id == NX_OWNER_NONE) {
        return NX_MEMORY_ERR_INVALID_ARGUMENT;
    }

    if (desc->size_bytes == 0) {
        return NX_MEMORY_ERR_INVALID_SIZE;
    }

    u64 requested_alignment = desc->alignment;
    if (requested_alignment == 0) {
        requested_alignment = NX_MEMORY_DEFAULT_ALIGNMENT;
    }

    if (!is_power_of_two(requested_alignment)) {
        return NX_MEMORY_ERR_INVALID_ALIGNMENT;
    }

    /*
     * Step 3 uses page-granular, page-aligned storage even though the current
     * backend is still the bootstrap arena. This keeps the object contract
     * compatible with a future real page-frame allocator.
     */
    u64 effective_alignment = requested_alignment;
    if (effective_alignment < NX_MEMORY_PAGE_SIZE) {
        effective_alignment = NX_MEMORY_PAGE_SIZE;
    }

    u64 capacity_bytes = 0;
    if (!align_up_checked(desc->size_bytes, NX_MEMORY_PAGE_SIZE, &capacity_bytes)) {
        return NX_MEMORY_ERR_SIZE_OVERFLOW;
    }

    if (nx_object_count() >= NX_OBJECT_REGISTRY_CAPACITY) {
        return NX_MEMORY_ERR_OBJECT_REGISTRY_FULL;
    }

    nx_memory *memory = (nx_memory *)kalloc_try(sizeof(nx_memory), 16);
    if (memory == NULL) {
        ++stats.allocation_failures;
        return NX_MEMORY_ERR_OUT_OF_MEMORY;
    }

    void *backing = kalloc_try((usize)capacity_bytes, (usize)effective_alignment);
    if (backing == NULL) {
        ++stats.allocation_failures;
        return NX_MEMORY_ERR_OUT_OF_MEMORY;
    }

    if (!nx_object_register_ex(
            &memory->object,
            NX_OBJECT_MEMORY,
            desc->name,
            NX_OBJECT_FLAG_KERNEL,
            owner_id,
            nx_memory_object_destroy)) {
        ++stats.allocation_failures;
        return NX_MEMORY_ERR_OBJECT_REGISTRY_FULL;
    }

    memory->backend = NX_MEMORY_BACKEND_EARLY_HEAP;
    memory->kernel_address = backing;
    memory->physical_base = (u64)(usize)backing;
    memory->size_bytes = desc->size_bytes;
    memory->capacity_bytes = capacity_bytes;
    memory->alignment = effective_alignment;
    memory->flags = desc->flags;

    if (desc->flags & NX_MEMORY_FLAG_ZERO_INIT) {
        zero_bytes(backing, capacity_bytes);
    }

    ++stats.created;
    ++stats.live;

    u64 next_requested = 0;
    if (add_u64_checked(stats.requested_bytes, desc->size_bytes, &next_requested)) {
        stats.requested_bytes = next_requested;
    } else {
        stats.requested_bytes = ~(u64)0;
    }

    u64 next_resident = 0;
    if (add_u64_checked(stats.resident_bytes, capacity_bytes, &next_resident)) {
        stats.resident_bytes = next_resident;
    } else {
        stats.resident_bytes = ~(u64)0;
    }

    if (stats.resident_bytes > stats.peak_resident_bytes) {
        stats.peak_resident_bytes = stats.resident_bytes;
    }

    *out_memory = memory;
    return NX_MEMORY_OK;
}

nx_memory *nx_memory_create(
    const char *name,
    u64 size_bytes,
    u64 alignment,
    u32 flags
) {
    return nx_memory_create_owned(
        name,
        size_bytes,
        alignment,
        flags,
        NX_OWNER_KERNEL
    );
}

nx_memory *nx_memory_create_owned(
    const char *name,
    u64 size_bytes,
    u64 alignment,
    u32 flags,
    nx_owner_id_t owner_id
) {
    nx_memory_desc desc;
    desc.name = name;
    desc.size_bytes = size_bytes;
    desc.alignment = alignment;
    desc.flags = flags;

    nx_memory *memory = NULL;
    const nx_memory_status status = nx_memory_try_create_owned(&desc, owner_id, &memory);
    if (status != NX_MEMORY_OK) {
        panic(nx_memory_status_name(status));
    }

    return memory;
}

nx_memory *nx_memory_lookup(nx_handle_t handle) {
    nx_object *object = nx_object_lookup(handle, NX_OBJECT_MEMORY);
    if (object == NULL) {
        return NULL;
    }

    /* nx_object is intentionally the first field of nx_memory. */
    return (nx_memory *)object;
}

bool nx_memory_retain(nx_handle_t handle) {
    nx_object *object = nx_object_lookup(handle, NX_OBJECT_MEMORY);
    return object != NULL && nx_object_retain(handle);
}

bool nx_memory_release(nx_handle_t handle) {
    nx_object *object = nx_object_lookup(handle, NX_OBJECT_MEMORY);
    return object != NULL && nx_object_release(handle);
}

bool nx_memory_pin(nx_handle_t handle) {
    nx_object *object = nx_object_lookup(handle, NX_OBJECT_MEMORY);
    return object != NULL && nx_object_pin(handle);
}

bool nx_memory_unpin(nx_handle_t handle) {
    nx_object *object = nx_object_lookup(handle, NX_OBJECT_MEMORY);
    return object != NULL && nx_object_unpin(handle);
}

bool nx_memory_contains_range(
    const nx_memory *memory,
    u64 offset,
    u64 length
) {
    if (memory == NULL || memory->kernel_address == NULL ||
        offset > memory->capacity_bytes) {
        return false;
    }

    return length <= memory->capacity_bytes - offset;
}

void *nx_memory_ptr(
    nx_memory *memory,
    u64 offset,
    u64 length
) {
    if (memory == NULL || memory->kernel_address == NULL ||
        !nx_memory_contains_range(memory, offset, length)) {
        return NULL;
    }

    return (void *)((u8 *)memory->kernel_address + offset);
}

const nx_memory_stats *nx_memory_get_stats(void) {
    return &stats;
}

const char *nx_memory_backend_name(nx_memory_backend backend) {
    switch (backend) {
        case NX_MEMORY_BACKEND_EARLY_HEAP: return "early-heap";
        case NX_MEMORY_BACKEND_EXTERNAL: return "external";
        default: return "unknown";
    }
}

const char *nx_memory_status_name(nx_memory_status status) {
    switch (status) {
        case NX_MEMORY_OK: return "memory ok";
        case NX_MEMORY_ERR_INVALID_ARGUMENT: return "memory invalid argument";
        case NX_MEMORY_ERR_INVALID_SIZE: return "memory invalid size";
        case NX_MEMORY_ERR_INVALID_ALIGNMENT: return "memory invalid alignment";
        case NX_MEMORY_ERR_SIZE_OVERFLOW: return "memory size overflow";
        case NX_MEMORY_ERR_OUT_OF_MEMORY: return "memory out of memory";
        case NX_MEMORY_ERR_OBJECT_REGISTRY_FULL: return "memory object registry full";
        default: return "memory unknown error";
    }
}
