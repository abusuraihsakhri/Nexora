#include <ai/mapping.h>
#include <ai/backing.h>
#include <ai/domain.h>
#include <ai/handle.h>
#include <ai/tensor.h>
#include <kernel/panic.h>

#define AI_MAPPING_SLOT_MASK        0xffull
#define AI_MAPPING_GENERATION_MASK  0xffffffull
#define AI_MAPPING_DOMAIN_MASK      0xffffffffull
#define AI_MAPPING_GENERATION_SHIFT 8u
#define AI_MAPPING_DOMAIN_SHIFT     32u

_Static_assert(AI_DOMAIN_MAX_MAPPINGS <= 255u,
               "mapping slot encoding supports at most 255 entries");

static u64 align_up_page(u64 value) {
    if (value == 0) return 0;
    return (value + AI_MAPPING_PAGE_SIZE - 1ull) &
           ~(AI_MAPPING_PAGE_SIZE - 1ull);
}

/* Generation 0 permanently retires a slot instead of wrapping stale IDs. */
static u32 next_generation(u32 current) {
    if (current >= (u32)AI_MAPPING_GENERATION_MASK) return 0u;
    return current + 1u;
}

static bool mapping_window_for_domain(
    u64 domain_id,
    u64 *window_start,
    u64 *window_end
) {
    if (domain_id == 0) return false;

    const u64 max_u64 = ~0ull;
    if (AI_MAPPING_VA_BASE > max_u64 - AI_MAPPING_DOMAIN_STRIDE) return false;

    u64 index = domain_id - 1ull;
    u64 max_index =
        (max_u64 - AI_MAPPING_VA_BASE - AI_MAPPING_DOMAIN_STRIDE) /
        AI_MAPPING_DOMAIN_STRIDE;
    if (index > max_index) return false;

    u64 start = AI_MAPPING_VA_BASE + index * AI_MAPPING_DOMAIN_STRIDE;
    u64 end = start + AI_MAPPING_DOMAIN_STRIDE;
    if (window_start) *window_start = start;
    if (window_end) *window_end = end;
    return true;
}

static ai_mapping_id_t encode_mapping(
    u32 domain_tag,
    u32 generation,
    u32 slot_index
) {
    u64 slot_token = (u64)(slot_index + 1u);
    u64 generation_bits =
        ((u64)generation & AI_MAPPING_GENERATION_MASK) << AI_MAPPING_GENERATION_SHIFT;
    u64 domain_bits =
        ((u64)domain_tag & AI_MAPPING_DOMAIN_MASK) << AI_MAPPING_DOMAIN_SHIFT;
    return domain_bits | generation_bits | slot_token;
}

static bool decode_slot(ai_mapping_id_t mapping, u32 *slot_index) {
    u32 slot_token = (u32)(mapping & AI_MAPPING_SLOT_MASK);
    if (slot_token == 0 || slot_token > AI_DOMAIN_MAX_MAPPINGS) return false;
    if (slot_index) *slot_index = slot_token - 1u;
    return true;
}

static bool mapping_domain_live(const ai_domain *domain) {
    if (!domain) return false;
    ai_domain_state state = ai_domain_state_get(domain);
    return state == AI_DOMAIN_ACTIVE || state == AI_DOMAIN_QUIESCING;
}

static bool mapping_protection_valid(ai_mapping_prot_t protection) {
    if ((protection & AI_MAP_PROT_READ) == 0) return false;
    return (protection & ~(AI_MAP_PROT_READ | AI_MAP_PROT_WRITE)) == 0;
}

/* Caller holds domain->mappings.lock. */
static ai_mapping_entry *lookup_entry_locked(
    const ai_domain *domain,
    ai_mapping_id_t mapping
) {
    if (!mapping_domain_live(domain) || mapping == AI_MAPPING_INVALID) return NULL;
    if (ai_mapping_domain_tag(mapping) != (u32)domain->id) return NULL;

    u32 slot_index = 0;
    if (!decode_slot(mapping, &slot_index)) return NULL;
    ai_mapping_entry *entry =
        (ai_mapping_entry *)&domain->mappings.entries[slot_index];
    if (!entry->occupied || !entry->tensor || !entry->backing) return NULL;
    if (entry->generation != ai_mapping_generation(mapping)) return NULL;
    return entry;
}

void ai_mapping_table_init(ai_mapping_table *table, u64 domain_id) {
    if (!table) return;
    ai_spinlock_init(&table->lock);
    table->live_count = 0;
    u64 window_start = 0;
    table->next_virtual_address =
        mapping_window_for_domain(domain_id, &window_start, NULL)
            ? window_start
            : 0;

    for (u32 i = 0; i < AI_DOMAIN_MAX_MAPPINGS; ++i) {
        ai_mapping_entry *entry = &table->entries[i];
        entry->tensor = NULL;
        entry->backing = NULL;
        entry->tensor_offset = 0;
        entry->length = 0;
        entry->mapped_bytes = 0;
        entry->virtual_base = 0;
        entry->physical_base = 0;
        entry->kernel_base = NULL;
        entry->generation = 1;
        entry->protection = 0;
        entry->occupied = false;
    }
}

ai_mapping_id_t ai_tensor_map(
    ai_domain *domain,
    u64 tensor_handle,
    ai_mapping_prot_t protection
) {
    if (!mapping_protection_valid(protection)) return AI_MAPPING_INVALID;

    ai_handle_rights_t required = AI_HANDLE_RIGHT_MAP | AI_HANDLE_RIGHT_READ;
    if ((protection & AI_MAP_PROT_WRITE) != 0) required |= AI_HANDLE_RIGHT_WRITE;

    ai_tensor *tensor = (ai_tensor *)ai_handle_acquire(
        domain,
        (ai_handle_t)tensor_handle,
        AI_HANDLE_OBJECT_TENSOR,
        required);
    if (!tensor) return AI_MAPPING_INVALID;

    u64 bytes = tensor->bytes;
    if (!ai_handle_release_object(tensor, AI_HANDLE_OBJECT_TENSOR)) {
        panic("tensor map size probe lost pin");
    }

    return ai_tensor_map_range(domain, tensor_handle, 0, bytes, protection);
}

ai_mapping_id_t ai_tensor_map_range(
    ai_domain *domain,
    u64 tensor_handle,
    u64 tensor_offset,
    u64 length,
    ai_mapping_prot_t protection
) {
    if (!domain || ai_domain_state_get(domain) != AI_DOMAIN_ACTIVE ||
        !mapping_protection_valid(protection) || length == 0) {
        return AI_MAPPING_INVALID;
    }

    ai_handle_rights_t required = AI_HANDLE_RIGHT_MAP | AI_HANDLE_RIGHT_READ;
    if ((protection & AI_MAP_PROT_WRITE) != 0) required |= AI_HANDLE_RIGHT_WRITE;

    /* This pin becomes the mapping's tensor reference on success. */
    ai_tensor *tensor = (ai_tensor *)ai_handle_acquire(
        domain,
        (ai_handle_t)tensor_handle,
        AI_HANDLE_OBJECT_TENSOR,
        required);
    if (!tensor) return AI_MAPPING_INVALID;

    ai_tensor_backing *backing = tensor->backing;
    bool valid = backing && backing->kernel_base;
    if (valid && (tensor_offset & (AI_MAPPING_PAGE_SIZE - 1ull)) != 0) valid = false;
    if (valid && (tensor_offset > tensor->bytes || length > tensor->bytes - tensor_offset)) {
        valid = false;
    }

    u64 backing_offset = 0;
    u64 mapped_bytes = 0;
    if (valid) {
        backing_offset = tensor->backing_offset + tensor_offset;
        if (backing_offset > backing->size_bytes ||
            length > backing->size_bytes - backing_offset ||
            (backing_offset & (AI_MAPPING_PAGE_SIZE - 1ull)) != 0) {
            valid = false;
        }
    }
    if (valid) {
        mapped_bytes = align_up_page(length);
        if (mapped_bytes == 0) valid = false;
    }

    if (!valid || !ai_backing_mapping_acquire(backing)) {
        (void)ai_handle_release_object(tensor, AI_HANDLE_OBJECT_TENSOR);
        return AI_MAPPING_INVALID;
    }

    ai_mapping_id_t result = AI_MAPPING_INVALID;
    ai_spin_lock(&domain->mappings.lock);

    /* Recheck after lock acquisition so quiesce has a clean linearization point. */
    if (ai_domain_state_get(domain) == AI_DOMAIN_ACTIVE) {
        u64 window_start = 0;
        u64 window_end = 0;
        u64 virtual_base = domain->mappings.next_virtual_address;

        if (mapping_window_for_domain(domain->id, &window_start, &window_end) &&
            virtual_base >= window_start && virtual_base <= window_end &&
            mapped_bytes <= window_end - virtual_base) {
            u32 slot_index = AI_DOMAIN_MAX_MAPPINGS;
            for (u32 i = 0; i < AI_DOMAIN_MAX_MAPPINGS; ++i) {
                if (!domain->mappings.entries[i].occupied &&
                    domain->mappings.entries[i].generation != 0u) {
                    slot_index = i;
                    break;
                }
            }

            if (slot_index != AI_DOMAIN_MAX_MAPPINGS) {
                ai_mapping_entry *entry = &domain->mappings.entries[slot_index];
                entry->tensor = tensor;
                entry->backing = backing;
                entry->tensor_offset = tensor_offset;
                entry->length = length;
                entry->mapped_bytes = mapped_bytes;
                entry->virtual_base = virtual_base;
                entry->physical_base = backing->physical_base + backing_offset;
                entry->kernel_base = (void *)((u8 *)backing->kernel_base + backing_offset);
                entry->protection = protection;
                entry->occupied = true;
                domain->mappings.live_count++;

                u64 next = virtual_base + mapped_bytes;
                if (AI_MAPPING_GUARD_BYTES <= window_end - next) {
                    next += AI_MAPPING_GUARD_BYTES;
                }
                domain->mappings.next_virtual_address = next;
                result = encode_mapping((u32)domain->id, entry->generation, slot_index);
            }
        }
    }

    ai_spin_unlock(&domain->mappings.lock);

    if (result == AI_MAPPING_INVALID) {
        (void)ai_backing_mapping_release(backing);
        (void)ai_handle_release_object(tensor, AI_HANDLE_OBJECT_TENSOR);
    }
    return result;
}

const ai_mapping_entry *ai_mapping_resolve(
    const ai_domain *domain,
    ai_mapping_id_t mapping
) {
    if (!domain) return NULL;
    ai_spin_lock((ai_spinlock *)&domain->mappings.lock);
    ai_mapping_entry *entry = lookup_entry_locked(domain, mapping);
    ai_spin_unlock((ai_spinlock *)&domain->mappings.lock);
    return entry;
}

bool ai_mapping_acquire_view(
    const ai_domain *domain,
    ai_mapping_id_t mapping,
    ai_mapping_view *view
) {
    if (!domain || !view) return false;
    view->valid = false;

    ai_spin_lock((ai_spinlock *)&domain->mappings.lock);
    ai_mapping_entry *entry = lookup_entry_locked(domain, mapping);
    if (!entry || !ai_tensor_get(entry->tensor)) {
        ai_spin_unlock((ai_spinlock *)&domain->mappings.lock);
        return false;
    }
    if (!ai_backing_get(entry->backing)) {
        (void)ai_tensor_put(entry->tensor);
        ai_spin_unlock((ai_spinlock *)&domain->mappings.lock);
        return false;
    }

    view->tensor = entry->tensor;
    view->backing = entry->backing;
    view->tensor_offset = entry->tensor_offset;
    view->length = entry->length;
    view->mapped_bytes = entry->mapped_bytes;
    view->virtual_base = entry->virtual_base;
    view->physical_base = entry->physical_base;
    view->kernel_base = entry->kernel_base;
    view->protection = entry->protection;
    view->valid = true;
    ai_spin_unlock((ai_spinlock *)&domain->mappings.lock);
    return true;
}

void ai_mapping_release_view(ai_mapping_view *view) {
    if (!view || !view->valid) return;
    ai_tensor *tensor = view->tensor;
    ai_tensor_backing *backing = view->backing;
    view->valid = false;
    view->tensor = NULL;
    view->backing = NULL;
    view->kernel_base = NULL;
    if (!ai_tensor_put(tensor)) panic("mapping view lost tensor pin");
    if (!ai_backing_put(backing)) panic("mapping view lost backing pin");
}

bool ai_tensor_unmap(ai_domain *domain, ai_mapping_id_t mapping) {
    if (!domain) return false;
    ai_tensor *tensor = NULL;
    ai_tensor_backing *backing = NULL;

    ai_spin_lock(&domain->mappings.lock);
    ai_mapping_entry *entry = lookup_entry_locked(domain, mapping);
    if (!entry || domain->mappings.live_count == 0) {
        ai_spin_unlock(&domain->mappings.lock);
        return false;
    }

    tensor = entry->tensor;
    backing = entry->backing;
    entry->tensor = NULL;
    entry->backing = NULL;
    entry->tensor_offset = 0;
    entry->length = 0;
    entry->mapped_bytes = 0;
    entry->virtual_base = 0;
    entry->physical_base = 0;
    entry->kernel_base = NULL;
    entry->protection = 0;
    entry->occupied = false;
    entry->generation = next_generation(entry->generation);
    domain->mappings.live_count--;
    ai_spin_unlock(&domain->mappings.lock);

    if (!ai_tensor_put(tensor)) panic("mapping lost tensor reference during unmap");
    if (!ai_backing_mapping_release(backing)) {
        panic("mapping lost backing reference during unmap");
    }
    return true;
}

u32 ai_mapping_live_count(const ai_domain *domain) {
    if (!domain) return 0;
    ai_spin_lock((ai_spinlock *)&domain->mappings.lock);
    u32 result = domain->mappings.live_count;
    ai_spin_unlock((ai_spinlock *)&domain->mappings.lock);
    return result;
}

u32 ai_mapping_domain_tag(ai_mapping_id_t mapping) {
    return (u32)((mapping >> AI_MAPPING_DOMAIN_SHIFT) & AI_MAPPING_DOMAIN_MASK);
}

u32 ai_mapping_generation(ai_mapping_id_t mapping) {
    return (u32)((mapping >> AI_MAPPING_GENERATION_SHIFT) & AI_MAPPING_GENERATION_MASK);
}

u32 ai_mapping_slot(ai_mapping_id_t mapping) {
    u32 slot_index = 0;
    return decode_slot(mapping, &slot_index) ? slot_index : AI_DOMAIN_MAX_MAPPINGS;
}

u64 ai_mapping_virtual_address(const ai_domain *domain, ai_mapping_id_t mapping) {
    if (!domain) return 0;
    ai_spin_lock((ai_spinlock *)&domain->mappings.lock);
    ai_mapping_entry *entry = lookup_entry_locked(domain, mapping);
    u64 result = entry ? entry->virtual_base : 0;
    ai_spin_unlock((ai_spinlock *)&domain->mappings.lock);
    return result;
}

u64 ai_mapping_physical_address(const ai_domain *domain, ai_mapping_id_t mapping) {
    if (!domain) return 0;
    ai_spin_lock((ai_spinlock *)&domain->mappings.lock);
    ai_mapping_entry *entry = lookup_entry_locked(domain, mapping);
    u64 result = entry ? entry->physical_base : 0;
    ai_spin_unlock((ai_spinlock *)&domain->mappings.lock);
    return result;
}

void *ai_mapping_kernel_address(const ai_domain *domain, ai_mapping_id_t mapping) {
    if (!domain) return NULL;
    ai_spin_lock((ai_spinlock *)&domain->mappings.lock);
    ai_mapping_entry *entry = lookup_entry_locked(domain, mapping);
    void *result = entry ? entry->kernel_base : NULL;
    ai_spin_unlock((ai_spinlock *)&domain->mappings.lock);
    return result;
}

bool ai_mapping_translate(
    const ai_domain *domain,
    ai_mapping_id_t mapping,
    u64 virtual_address,
    u64 *physical_address
) {
    if (!domain || !physical_address) return false;
    bool result = false;
    ai_spin_lock((ai_spinlock *)&domain->mappings.lock);
    ai_mapping_entry *entry = lookup_entry_locked(domain, mapping);
    if (entry && virtual_address >= entry->virtual_base) {
        u64 offset = virtual_address - entry->virtual_base;
        if (offset < entry->length) {
            *physical_address = entry->physical_base + offset;
            result = true;
        }
    }
    ai_spin_unlock((ai_spinlock *)&domain->mappings.lock);
    return result;
}
