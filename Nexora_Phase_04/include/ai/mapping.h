#ifndef AIKERNEL_AI_MAPPING_H
#define AIKERNEL_AI_MAPPING_H

#include <kernel/types.h>
#include <kernel/spinlock.h>

#define AI_DOMAIN_MAX_MAPPINGS 64u
#define AI_MAPPING_PAGE_SIZE 4096ull
#define AI_MAPPING_VA_BASE       0x0000200000000000ull
#define AI_MAPPING_DOMAIN_STRIDE 0x0000000100000000ull
#define AI_MAPPING_GUARD_BYTES   AI_MAPPING_PAGE_SIZE

typedef u64 ai_mapping_id_t;
typedef u32 ai_mapping_prot_t;

#define AI_MAPPING_INVALID ((ai_mapping_id_t)0)

enum {
    AI_MAP_PROT_READ  = 1u << 0,
    AI_MAP_PROT_WRITE = 1u << 1
};

struct ai_domain;
struct ai_tensor;
struct ai_tensor_backing;

typedef struct {
    struct ai_tensor *tensor;
    struct ai_tensor_backing *backing;
    u64 tensor_offset;
    u64 length;
    u64 mapped_bytes;
    u64 virtual_base;
    u64 physical_base;
    void *kernel_base;
    /* Generation 0 is reserved for a permanently exhausted slot. */
    u32 generation;
    ai_mapping_prot_t protection;
    bool occupied;
} ai_mapping_entry;

typedef struct {
    ai_mapping_entry entries[AI_DOMAIN_MAX_MAPPINGS];
    u32 live_count;
    u64 next_virtual_address;
    ai_spinlock lock;
} ai_mapping_table;

/* Stable, reference-pinned copy suitable for concurrent users. */
typedef struct {
    struct ai_tensor *tensor;
    struct ai_tensor_backing *backing;
    u64 tensor_offset;
    u64 length;
    u64 mapped_bytes;
    u64 virtual_base;
    u64 physical_base;
    void *kernel_base;
    ai_mapping_prot_t protection;
    bool valid;
} ai_mapping_view;

void ai_mapping_table_init(ai_mapping_table *table, u64 domain_id);

ai_mapping_id_t ai_tensor_map(
    struct ai_domain *domain,
    u64 tensor_handle,
    ai_mapping_prot_t protection
);

ai_mapping_id_t ai_tensor_map_range(
    struct ai_domain *domain,
    u64 tensor_handle,
    u64 tensor_offset,
    u64 length,
    ai_mapping_prot_t protection
);

/* Non-owning diagnostic lookup. Prefer ai_mapping_acquire_view concurrently. */
const ai_mapping_entry *ai_mapping_resolve(
    const struct ai_domain *domain,
    ai_mapping_id_t mapping
);

bool ai_mapping_acquire_view(
    const struct ai_domain *domain,
    ai_mapping_id_t mapping,
    ai_mapping_view *view
);
void ai_mapping_release_view(ai_mapping_view *view);

bool ai_tensor_unmap(struct ai_domain *domain, ai_mapping_id_t mapping);

u32 ai_mapping_live_count(const struct ai_domain *domain);
u32 ai_mapping_domain_tag(ai_mapping_id_t mapping);
u32 ai_mapping_generation(ai_mapping_id_t mapping);
u32 ai_mapping_slot(ai_mapping_id_t mapping);

u64 ai_mapping_virtual_address(
    const struct ai_domain *domain,
    ai_mapping_id_t mapping
);

u64 ai_mapping_physical_address(
    const struct ai_domain *domain,
    ai_mapping_id_t mapping
);

void *ai_mapping_kernel_address(
    const struct ai_domain *domain,
    ai_mapping_id_t mapping
);

bool ai_mapping_translate(
    const struct ai_domain *domain,
    ai_mapping_id_t mapping,
    u64 virtual_address,
    u64 *physical_address
);

#endif
