#ifndef NEXORA_KERNEL_MEMORY_OBJECT_H
#define NEXORA_KERNEL_MEMORY_OBJECT_H

#include <kernel/types.h>
#include <kernel/object.h>

#define NX_MEMORY_PAGE_SIZE 4096u
#define NX_MEMORY_DEFAULT_ALIGNMENT 64u

typedef enum {
    NX_MEMORY_BACKEND_EARLY_HEAP = 0,
    NX_MEMORY_BACKEND_EXTERNAL,
    NX_MEMORY_BACKEND_COUNT
} nx_memory_backend;

typedef enum {
    NX_MEMORY_FLAG_NONE            = 0,
    NX_MEMORY_FLAG_ZERO_INIT       = 1u << 0,
    NX_MEMORY_FLAG_READONLY        = 1u << 1,
    NX_MEMORY_FLAG_PINNED          = 1u << 2,
    NX_MEMORY_FLAG_EMULATED_DEVICE = 1u << 3
} nx_memory_flags;

typedef enum {
    NX_MEMORY_OK = 0,
    NX_MEMORY_ERR_INVALID_ARGUMENT,
    NX_MEMORY_ERR_INVALID_SIZE,
    NX_MEMORY_ERR_INVALID_ALIGNMENT,
    NX_MEMORY_ERR_SIZE_OVERFLOW,
    NX_MEMORY_ERR_OUT_OF_MEMORY,
    NX_MEMORY_ERR_OBJECT_REGISTRY_FULL
} nx_memory_status;

typedef struct {
    const char *name;
    u64 size_bytes;
    u64 alignment;
    u32 flags;
} nx_memory_desc;

typedef struct nx_memory {
    nx_object object;

    nx_memory_backend backend;
    void *kernel_address;

    /*
     * The bootstrap kernel identity-maps the first 1 GiB, and the early heap
     * lives inside that mapped image. During this bootstrap phase this value
     * is therefore also the physical base used by the CPU. A future PMM/VMM
     * backend can replace this assumption without changing tensor handles.
     */
    u64 physical_base;

    u64 size_bytes;
    u64 capacity_bytes;
    u64 alignment;
    u32 flags;
} nx_memory;

typedef struct {
    u64 created;
    u64 destroyed;
    u64 live;
    u64 requested_bytes;
    u64 resident_bytes;
    u64 peak_resident_bytes;
    u64 allocation_failures;
} nx_memory_stats;

void nx_memory_system_init(void);

nx_memory_status nx_memory_try_create(
    const nx_memory_desc *desc,
    nx_memory **out_memory
);

nx_memory_status nx_memory_try_create_owned(
    const nx_memory_desc *desc,
    nx_owner_id_t owner_id,
    nx_memory **out_memory
);

nx_memory *nx_memory_create(
    const char *name,
    u64 size_bytes,
    u64 alignment,
    u32 flags
);

nx_memory *nx_memory_create_owned(
    const char *name,
    u64 size_bytes,
    u64 alignment,
    u32 flags,
    nx_owner_id_t owner_id
);

nx_memory *nx_memory_lookup(nx_handle_t handle);

bool nx_memory_retain(nx_handle_t handle);
bool nx_memory_release(nx_handle_t handle);
bool nx_memory_pin(nx_handle_t handle);
bool nx_memory_unpin(nx_handle_t handle);

bool nx_memory_contains_range(
    const nx_memory *memory,
    u64 offset,
    u64 length
);

void *nx_memory_ptr(
    nx_memory *memory,
    u64 offset,
    u64 length
);

const nx_memory_stats *nx_memory_get_stats(void);

const char *nx_memory_backend_name(nx_memory_backend backend);
const char *nx_memory_status_name(nx_memory_status status);

#endif
