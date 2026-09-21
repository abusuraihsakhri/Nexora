#ifndef AIKERNEL_AI_BACKING_H
#define AIKERNEL_AI_BACKING_H

#include <kernel/types.h>

#define AI_BACKING_PAGE_SIZE 4096ull
#define AI_MAX_BACKINGS 64u

typedef enum {
    AI_BACKING_RAM = 0,
    AI_BACKING_DEVICE,
    AI_BACKING_PINNED,
    AI_BACKING_REMOTE
} ai_backing_kind;

typedef enum {
    AI_BACKING_ZEROED = 1u << 0
} ai_backing_flags;

typedef enum {
    AI_BACKING_LIVE = 0,
    AI_BACKING_RETIRED
} ai_backing_lifetime_state;

typedef struct ai_tensor_backing {
    u64 id;
    ai_backing_kind kind;

    /* Logical payload size requested by the tensor layer. */
    u64 size_bytes;

    /* Page-rounded storage size actually reserved. */
    u64 allocation_bytes;
    u64 page_count;

    /*
     * RAM backing comes from the early page-aligned allocator. On the current
     * identity-mapped kernel this address is also the physical base. Host tests
     * treat physical_base as an opaque physical-address token.
     */
    void *kernel_base;
    u64 physical_base;

    /*
     * Step 8 lifetime model (atomic/SMP-safe):
     * - refcount counts creator/tensor/handle/mapping references.
     * - mapping_count is a diagnostic/invariant count of active mappings.
     * - a backing is retired only after refcount reaches zero.
     * Every live mapping owns one reference, so refcount cannot legitimately
     * reach zero while mapping_count is nonzero.
     */
    u64 refcount;
    u64 mapping_count;
    ai_backing_lifetime_state lifetime_state;

    u32 flags;
} ai_tensor_backing;

void ai_backing_system_init(void);

ai_tensor_backing *ai_backing_create_ram(u64 size_bytes, u32 flags);

bool ai_backing_get(ai_tensor_backing *backing);
bool ai_backing_put(ai_tensor_backing *backing);
bool ai_backing_is_live(const ai_tensor_backing *backing);
u64 ai_backing_refcount(const ai_tensor_backing *backing);
u64 ai_backing_mapping_count(const ai_tensor_backing *backing);

bool ai_backing_mapping_acquire(ai_tensor_backing *backing);
bool ai_backing_mapping_release(ai_tensor_backing *backing);

u64 ai_backing_count(void);
u64 ai_backing_physical_at(const ai_tensor_backing *backing, u64 offset);
void *ai_backing_kernel_at(const ai_tensor_backing *backing, u64 offset);

const char *ai_backing_kind_name(ai_backing_kind kind);
const char *ai_backing_lifetime_state_name(ai_backing_lifetime_state state);

#endif
