#include <ai/handle.h>
#include <ai/backing.h>
#include <ai/domain.h>
#include <ai/tensor.h>
#include <kernel/panic.h>

#define AI_HANDLE_SLOT_MASK        0xffull
#define AI_HANDLE_GENERATION_MASK  0xffffffull
#define AI_HANDLE_DOMAIN_MASK      0xffffffffull
#define AI_HANDLE_GENERATION_SHIFT 8u
#define AI_HANDLE_DOMAIN_SHIFT     32u

_Static_assert(AI_DOMAIN_MAX_HANDLES <= 255u,
               "handle slot encoding supports at most 255 entries");

/*
 * Generation 0 is reserved as a permanently exhausted slot marker.
 * Never wrap back to generation 1: doing so could make a very old stale
 * capability token valid again after 2^24 slot reuses (ABA).
 */
static u32 next_generation(u32 current) {
    if (current >= (u32)AI_HANDLE_GENERATION_MASK) return 0u;
    return current + 1u;
}

static ai_handle_t encode_handle(u32 domain_tag, u32 generation, u32 slot_index) {
    u64 slot_token = (u64)(slot_index + 1u);
    u64 generation_bits =
        ((u64)generation & AI_HANDLE_GENERATION_MASK) << AI_HANDLE_GENERATION_SHIFT;
    u64 domain_bits =
        ((u64)domain_tag & AI_HANDLE_DOMAIN_MASK) << AI_HANDLE_DOMAIN_SHIFT;
    return domain_bits | generation_bits | slot_token;
}

static bool decode_slot(ai_handle_t handle, u32 *slot_index) {
    u32 slot_token = (u32)(handle & AI_HANDLE_SLOT_MASK);
    if (slot_token == 0 || slot_token > AI_DOMAIN_MAX_HANDLES) return false;
    if (slot_index) *slot_index = slot_token - 1u;
    return true;
}

static bool domain_accepts_existing_handles(const ai_domain *domain) {
    if (!domain) return false;
    ai_domain_state state = ai_domain_state_get(domain);
    return state == AI_DOMAIN_ACTIVE || state == AI_DOMAIN_QUIESCING;
}

static bool rights_are_subset(ai_handle_rights_t candidate, ai_handle_rights_t current) {
    return (candidate & current) == candidate;
}

static bool object_reference_acquire(void *object, ai_handle_object_type type) {
    if (!object) return false;
    switch (type) {
        case AI_HANDLE_OBJECT_TENSOR:
            return ai_tensor_get((ai_tensor *)object);
        case AI_HANDLE_OBJECT_BACKING:
            return ai_backing_get((ai_tensor_backing *)object);
        case AI_HANDLE_OBJECT_WORK:
        case AI_HANDLE_OBJECT_GENERIC:
            return true;
        default:
            return false;
    }
}

static bool object_reference_release(void *object, ai_handle_object_type type) {
    if (!object) return false;
    switch (type) {
        case AI_HANDLE_OBJECT_TENSOR:
            return ai_tensor_put((ai_tensor *)object);
        case AI_HANDLE_OBJECT_BACKING:
            return ai_backing_put((ai_tensor_backing *)object);
        case AI_HANDLE_OBJECT_WORK:
        case AI_HANDLE_OBJECT_GENERIC:
            return true;
        default:
            return false;
    }
}

static bool rights_compatible_with_object(
    void *object,
    ai_handle_object_type type,
    ai_handle_rights_t rights
) {
    if (!object || !ai_handle_rights_valid(rights)) return false;
    if (type == AI_HANDLE_OBJECT_TENSOR) {
        const ai_tensor *tensor = (const ai_tensor *)object;
        if ((tensor->flags & AI_TENSOR_READONLY) != 0 &&
            (rights & AI_HANDLE_RIGHT_WRITE) != 0) {
            return false;
        }
    }
    return true;
}

/* Caller must hold domain->handles.lock. */
static ai_handle_entry *lookup_entry_locked(
    const ai_domain *domain,
    ai_handle_t handle,
    bool allow_revoked
) {
    if (!domain_accepts_existing_handles(domain) || handle == AI_HANDLE_INVALID) {
        return NULL;
    }
    if (ai_handle_domain_tag(handle) != (u32)domain->id) return NULL;

    u32 slot_index = 0;
    if (!decode_slot(handle, &slot_index)) return NULL;

    ai_handle_entry *entry =
        (ai_handle_entry *)&domain->handles.entries[slot_index];
    if (!entry->occupied || !entry->object) return NULL;
    if (entry->generation != ai_handle_generation(handle)) return NULL;
    if (!allow_revoked && entry->revoked) return NULL;
    return entry;
}

static bool entry_authorizes(
    const ai_handle_entry *entry,
    ai_handle_object_type expected_type,
    ai_handle_rights_t required_rights
) {
    if (!entry || entry->revoked) return false;
    if (expected_type != AI_HANDLE_OBJECT_ANY && entry->type != expected_type) {
        return false;
    }
    return rights_are_subset(required_rights, entry->rights);
}

/* Caller holds table lock. Returns the detached reference for release outside. */
static bool detach_entry_locked(
    ai_handle_table *table,
    ai_handle_entry *entry,
    void **object,
    ai_handle_object_type *type
) {
    if (!table || !entry || !entry->occupied || !entry->object ||
        table->live_count == 0) {
        return false;
    }

    if (object) *object = entry->object;
    if (type) *type = entry->type;

    if (entry->revoked) {
        if (table->revoked_count == 0) panic("handle revoked-count underflow");
        table->revoked_count--;
    }

    entry->object = NULL;
    entry->type = AI_HANDLE_OBJECT_NONE;
    entry->rights = 0;
    entry->occupied = false;
    entry->revoked = false;
    entry->generation = next_generation(entry->generation);
    table->live_count--;
    return true;
}

void ai_handle_table_init(ai_handle_table *table) {
    if (!table) return;
    ai_spinlock_init(&table->lock);
    table->live_count = 0;
    table->revoked_count = 0;
    for (u32 i = 0; i < AI_DOMAIN_MAX_HANDLES; ++i) {
        table->entries[i].object = NULL;
        table->entries[i].generation = 1;
        table->entries[i].type = AI_HANDLE_OBJECT_NONE;
        table->entries[i].rights = 0;
        table->entries[i].occupied = false;
        table->entries[i].revoked = false;
    }
}

bool ai_handle_rights_valid(ai_handle_rights_t rights) {
    return rights != 0 && (rights & ~AI_HANDLE_RIGHT_ALL) == 0;
}

ai_handle_t ai_handle_install(
    ai_domain *domain,
    void *object,
    ai_handle_object_type type,
    ai_handle_rights_t rights
) {
    if (!domain || !object || ai_domain_state_get(domain) != AI_DOMAIN_ACTIVE) {
        return AI_HANDLE_INVALID;
    }
    if (type == AI_HANDLE_OBJECT_NONE || type == AI_HANDLE_OBJECT_ANY) {
        return AI_HANDLE_INVALID;
    }
    if (!rights_compatible_with_object(object, type, rights)) {
        return AI_HANDLE_INVALID;
    }
    if (domain->id == 0 || domain->id > AI_DOMAIN_ID_MAX) return AI_HANDLE_INVALID;

    ai_spin_lock(&domain->handles.lock);
    if (ai_domain_state_get(domain) != AI_DOMAIN_ACTIVE) {
        ai_spin_unlock(&domain->handles.lock);
        return AI_HANDLE_INVALID;
    }

    for (u32 i = 0; i < AI_DOMAIN_MAX_HANDLES; ++i) {
        ai_handle_entry *entry = &domain->handles.entries[i];
        if (entry->occupied || entry->generation == 0u) continue;

        if (!object_reference_acquire(object, type)) {
            ai_spin_unlock(&domain->handles.lock);
            return AI_HANDLE_INVALID;
        }

        entry->object = object;
        entry->type = type;
        entry->rights = rights;
        entry->revoked = false;
        entry->occupied = true;
        domain->handles.live_count++;
        ai_handle_t result = encode_handle((u32)domain->id, entry->generation, i);
        ai_spin_unlock(&domain->handles.lock);
        return result;
    }

    ai_spin_unlock(&domain->handles.lock);
    return AI_HANDLE_INVALID;
}

void *ai_handle_resolve(
    const ai_domain *domain,
    ai_handle_t handle,
    ai_handle_object_type expected_type,
    ai_handle_rights_t required_rights
) {
    if (!domain || (required_rights != 0 && !ai_handle_rights_valid(required_rights))) {
        return NULL;
    }

    ai_spin_lock((ai_spinlock *)&domain->handles.lock);
    ai_handle_entry *entry = lookup_entry_locked(domain, handle, false);
    void *object = entry_authorizes(entry, expected_type, required_rights)
        ? entry->object : NULL;
    ai_spin_unlock((ai_spinlock *)&domain->handles.lock);
    return object;
}

void *ai_handle_acquire(
    const ai_domain *domain,
    ai_handle_t handle,
    ai_handle_object_type expected_type,
    ai_handle_rights_t required_rights
) {
    if (!domain || (required_rights != 0 && !ai_handle_rights_valid(required_rights))) {
        return NULL;
    }

    ai_spin_lock((ai_spinlock *)&domain->handles.lock);
    ai_handle_entry *entry = lookup_entry_locked(domain, handle, false);
    void *object = NULL;
    if (entry_authorizes(entry, expected_type, required_rights) &&
        object_reference_acquire(entry->object, entry->type)) {
        object = entry->object;
    }
    ai_spin_unlock((ai_spinlock *)&domain->handles.lock);
    return object;
}

bool ai_handle_release_object(void *object, ai_handle_object_type type) {
    return object_reference_release(object, type);
}

bool ai_handle_is_valid(
    const ai_domain *domain,
    ai_handle_t handle,
    ai_handle_object_type expected_type,
    ai_handle_rights_t required_rights
) {
    return ai_handle_resolve(domain, handle, expected_type, required_rights) != NULL;
}

bool ai_handle_has_rights(
    const ai_domain *domain,
    ai_handle_t handle,
    ai_handle_rights_t required_rights
) {
    if (!domain || !ai_handle_rights_valid(required_rights)) return false;
    ai_spin_lock((ai_spinlock *)&domain->handles.lock);
    ai_handle_entry *entry = lookup_entry_locked(domain, handle, false);
    bool result = entry && rights_are_subset(required_rights, entry->rights);
    ai_spin_unlock((ai_spinlock *)&domain->handles.lock);
    return result;
}

ai_handle_rights_t ai_handle_get_rights(const ai_domain *domain, ai_handle_t handle) {
    if (!domain) return 0;
    ai_spin_lock((ai_spinlock *)&domain->handles.lock);
    ai_handle_entry *entry = lookup_entry_locked(domain, handle, false);
    ai_handle_rights_t rights = entry ? entry->rights : 0;
    ai_spin_unlock((ai_spinlock *)&domain->handles.lock);
    return rights;
}

bool ai_handle_restrict_rights(
    ai_domain *domain,
    ai_handle_t handle,
    ai_handle_rights_t new_rights
) {
    if (!domain || !ai_handle_rights_valid(new_rights)) return false;
    ai_spin_lock(&domain->handles.lock);
    ai_handle_entry *entry = lookup_entry_locked(domain, handle, false);
    if (!entry || !rights_are_subset(new_rights, entry->rights)) {
        ai_spin_unlock(&domain->handles.lock);
        return false;
    }
    entry->rights = new_rights;
    ai_spin_unlock(&domain->handles.lock);
    return true;
}

typedef struct {
    void *object;
    ai_handle_object_type type;
    ai_handle_rights_t rights;
    bool valid;
} ai_handle_snapshot;

static bool domains_allow_delegation(
    const ai_domain *source_domain,
    const ai_domain *target_domain
) {
    if (!source_domain || !target_domain || source_domain == target_domain) return false;
    return ai_domain_state_get(source_domain) == AI_DOMAIN_ACTIVE &&
           ai_domain_state_get(target_domain) == AI_DOMAIN_ACTIVE;
}

static ai_handle_snapshot source_snapshot_acquire(
    ai_domain *source_domain,
    ai_handle_t source_handle,
    ai_handle_rights_t requested_rights,
    ai_handle_rights_t delegation_right
) {
    ai_handle_snapshot snapshot = {0};
    if (!source_domain || !ai_handle_rights_valid(requested_rights)) return snapshot;

    ai_spin_lock(&source_domain->handles.lock);
    if (ai_domain_state_get(source_domain) != AI_DOMAIN_ACTIVE) {
        ai_spin_unlock(&source_domain->handles.lock);
        return snapshot;
    }

    ai_handle_entry *entry = lookup_entry_locked(source_domain, source_handle, false);
    if (!entry ||
        !rights_are_subset(delegation_right, entry->rights) ||
        !rights_are_subset(requested_rights, entry->rights) ||
        !object_reference_acquire(entry->object, entry->type)) {
        ai_spin_unlock(&source_domain->handles.lock);
        return snapshot;
    }

    snapshot.object = entry->object;
    snapshot.type = entry->type;
    snapshot.rights = entry->rights;
    snapshot.valid = true;
    ai_spin_unlock(&source_domain->handles.lock);
    return snapshot;
}

ai_handle_t ai_handle_share(
    ai_domain *source_domain,
    ai_handle_t source_handle,
    ai_domain *target_domain,
    ai_handle_rights_t requested_rights
) {
    if (!domains_allow_delegation(source_domain, target_domain)) {
        return AI_HANDLE_INVALID;
    }

    ai_handle_snapshot snapshot = source_snapshot_acquire(
        source_domain,
        source_handle,
        requested_rights,
        AI_HANDLE_RIGHT_SHARE);
    if (!snapshot.valid) return AI_HANDLE_INVALID;

    ai_handle_t target = ai_handle_install(
        target_domain, snapshot.object, snapshot.type, requested_rights);

    if (!object_reference_release(snapshot.object, snapshot.type)) {
        panic("share temporary pin release failed");
    }
    return target;
}

ai_handle_t ai_handle_transfer(
    ai_domain *source_domain,
    ai_handle_t source_handle,
    ai_domain *target_domain,
    ai_handle_rights_t requested_rights
) {
    if (!domains_allow_delegation(source_domain, target_domain)) {
        return AI_HANDLE_INVALID;
    }

    ai_handle_snapshot snapshot = source_snapshot_acquire(
        source_domain,
        source_handle,
        requested_rights,
        AI_HANDLE_RIGHT_TRANSFER);
    if (!snapshot.valid) return AI_HANDLE_INVALID;

    ai_handle_t target = ai_handle_install(
        target_domain, snapshot.object, snapshot.type, requested_rights);
    if (target == AI_HANDLE_INVALID) {
        (void)object_reference_release(snapshot.object, snapshot.type);
        return AI_HANDLE_INVALID;
    }

    void *source_object = NULL;
    ai_handle_object_type source_type = AI_HANDLE_OBJECT_NONE;
    bool committed = false;

    ai_spin_lock(&source_domain->handles.lock);
    ai_handle_entry *entry = lookup_entry_locked(source_domain, source_handle, false);
    if (entry && entry->object == snapshot.object && entry->type == snapshot.type &&
        rights_are_subset(AI_HANDLE_RIGHT_TRANSFER, entry->rights) &&
        rights_are_subset(requested_rights, entry->rights)) {
        committed = detach_entry_locked(
            &source_domain->handles, entry, &source_object, &source_type);
    }
    ai_spin_unlock(&source_domain->handles.lock);

    if (!committed) {
        (void)ai_handle_close(target_domain, target);
        (void)object_reference_release(snapshot.object, snapshot.type);
        return AI_HANDLE_INVALID;
    }

    if (!object_reference_release(source_object, source_type)) {
        panic("transfer source reference release failed");
    }
    if (!object_reference_release(snapshot.object, snapshot.type)) {
        panic("transfer temporary pin release failed");
    }
    return target;
}

bool ai_handle_revoke(ai_domain *domain, ai_handle_t handle) {
    if (!domain) return false;
    ai_spin_lock(&domain->handles.lock);
    ai_handle_entry *entry = lookup_entry_locked(domain, handle, true);
    if (!entry || entry->revoked) {
        ai_spin_unlock(&domain->handles.lock);
        return false;
    }

    entry->revoked = true;
    entry->rights = 0;
    domain->handles.revoked_count++;
    ai_spin_unlock(&domain->handles.lock);
    return true;
}

bool ai_handle_is_revoked(const ai_domain *domain, ai_handle_t handle) {
    if (!domain) return false;
    ai_spin_lock((ai_spinlock *)&domain->handles.lock);
    ai_handle_entry *entry = lookup_entry_locked(domain, handle, true);
    bool result = entry && entry->revoked;
    ai_spin_unlock((ai_spinlock *)&domain->handles.lock);
    return result;
}

u32 ai_handle_reap_revoked(ai_domain *domain) {
    if (!domain) return 0;
    u32 reaped = 0;

    for (;;) {
        void *object = NULL;
        ai_handle_object_type type = AI_HANDLE_OBJECT_NONE;
        bool found = false;

        ai_spin_lock(&domain->handles.lock);
        for (u32 i = 0; i < AI_DOMAIN_MAX_HANDLES; ++i) {
            ai_handle_entry *entry = &domain->handles.entries[i];
            if (entry->occupied && entry->revoked) {
                found = detach_entry_locked(
                    &domain->handles, entry, &object, &type);
                break;
            }
        }
        ai_spin_unlock(&domain->handles.lock);

        if (!found) break;
        if (!object_reference_release(object, type)) {
            panic("revoked handle reap failed to release object reference");
        }
        reaped++;
    }
    return reaped;
}

bool ai_handle_close(ai_domain *domain, ai_handle_t handle) {
    if (!domain_accepts_existing_handles(domain) || handle == AI_HANDLE_INVALID) {
        return false;
    }

    void *object = NULL;
    ai_handle_object_type type = AI_HANDLE_OBJECT_NONE;

    ai_spin_lock(&domain->handles.lock);
    ai_handle_entry *entry = lookup_entry_locked(domain, handle, true);
    bool detached = detach_entry_locked(
        &domain->handles, entry, &object, &type);
    ai_spin_unlock(&domain->handles.lock);

    if (!detached) return false;
    if (!object_reference_release(object, type)) {
        panic("handle close failed to release object reference");
    }
    return true;
}

u32 ai_handle_live_count(const ai_domain *domain) {
    if (!domain) return 0;
    ai_spin_lock((ai_spinlock *)&domain->handles.lock);
    u32 result = domain->handles.live_count;
    ai_spin_unlock((ai_spinlock *)&domain->handles.lock);
    return result;
}

u32 ai_handle_revoked_count(const ai_domain *domain) {
    if (!domain) return 0;
    ai_spin_lock((ai_spinlock *)&domain->handles.lock);
    u32 result = domain->handles.revoked_count;
    ai_spin_unlock((ai_spinlock *)&domain->handles.lock);
    return result;
}

u32 ai_handle_domain_tag(ai_handle_t handle) {
    return (u32)((handle >> AI_HANDLE_DOMAIN_SHIFT) & AI_HANDLE_DOMAIN_MASK);
}

u32 ai_handle_generation(ai_handle_t handle) {
    return (u32)((handle >> AI_HANDLE_GENERATION_SHIFT) & AI_HANDLE_GENERATION_MASK);
}

u32 ai_handle_slot(ai_handle_t handle) {
    u32 slot_index = 0;
    return decode_slot(handle, &slot_index) ? slot_index : 0xffffffffu;
}

const char *ai_handle_object_type_name(ai_handle_object_type type) {
    switch (type) {
        case AI_HANDLE_OBJECT_NONE: return "NONE";
        case AI_HANDLE_OBJECT_TENSOR: return "TENSOR";
        case AI_HANDLE_OBJECT_BACKING: return "BACKING";
        case AI_HANDLE_OBJECT_WORK: return "WORK";
        case AI_HANDLE_OBJECT_GENERIC: return "GENERIC";
        case AI_HANDLE_OBJECT_ANY: return "ANY";
        default: return "UNKNOWN";
    }
}
