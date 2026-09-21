#include <ai/domain.h>
#include <kernel/atomic.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>

static ai_domain *registry[AI_MAX_DOMAINS];
static u64 domain_count = 0;
static ai_domain_id_t next_domain_id = 1;
static ai_spinlock registry_lock = AI_SPINLOCK_INITIALIZER;

static bool domain_is_live(const ai_domain *domain) {
    return domain && ai_domain_state_get(domain) != AI_DOMAIN_DEAD;
}

void ai_domain_system_init(void) {
    ai_spinlock_init(&registry_lock);
    ai_spin_lock(&registry_lock);
    for (u32 i = 0; i < AI_MAX_DOMAINS; ++i) registry[i] = NULL;
    domain_count = 0;
    next_domain_id = 1;
    ai_spin_unlock(&registry_lock);
}

ai_domain *ai_domain_create(
    const char *name,
    u32 flags,
    ai_domain_limits limits
) {
    ai_domain *domain = (ai_domain *)kalloc(sizeof(ai_domain), 16);

    ai_spin_lock(&registry_lock);
    if (domain_count >= AI_MAX_DOMAINS) {
        ai_spin_unlock(&registry_lock);
        panic("domain registry full");
    }
    if (next_domain_id == 0 || next_domain_id > AI_DOMAIN_ID_MAX) {
        ai_spin_unlock(&registry_lock);
        panic("domain id space exhausted");
    }

    domain->id = next_domain_id++;
    domain->name = name;
    ai_atomic_store_u32((volatile u32 *)&domain->state, (u32)AI_DOMAIN_NEW);
    domain->flags = flags;
    domain->limits = limits;
    ai_atomic_store_u64(&domain->memory_used_bytes, 0);
    ai_atomic_store_u32(&domain->tensor_count, 0);
    ai_atomic_store_u32(&domain->work_node_count, 0);
    ai_handle_table_init(&domain->handles);
    ai_mapping_table_init(&domain->mappings, domain->id);

    for (u32 i = 0; i < AI_MAX_DOMAINS; ++i) {
        if (registry[i] == NULL) {
            registry[i] = domain;
            domain_count++;
            ai_spin_unlock(&registry_lock);
            return domain;
        }
    }

    ai_spin_unlock(&registry_lock);
    panic("domain registry invariant violated");
    return NULL;
}

ai_domain *ai_domain_lookup(ai_domain_id_t id) {
    if (id == 0) return NULL;
    ai_spin_lock(&registry_lock);
    ai_domain *result = NULL;
    for (u32 i = 0; i < AI_MAX_DOMAINS; ++i) {
        ai_domain *domain = registry[i];
        if (domain && domain->id == id && ai_domain_state_get(domain) != AI_DOMAIN_DEAD) {
            result = domain;
            break;
        }
    }
    ai_spin_unlock(&registry_lock);
    return result;
}

bool ai_domain_activate(ai_domain *domain) {
    if (!domain) return false;
    u32 expected = (u32)AI_DOMAIN_NEW;
    return ai_atomic_compare_exchange_u32(
        (volatile u32 *)&domain->state,
        &expected,
        (u32)AI_DOMAIN_ACTIVE);
}

bool ai_domain_begin_quiesce(ai_domain *domain) {
    if (!domain) return false;
    u32 expected = (u32)AI_DOMAIN_ACTIVE;
    return ai_atomic_compare_exchange_u32(
        (volatile u32 *)&domain->state,
        &expected,
        (u32)AI_DOMAIN_QUIESCING);
}

bool ai_domain_destroy(ai_domain *domain) {
    if (!domain) return false;
    ai_domain_state state = ai_domain_state_get(domain);
    if (state == AI_DOMAIN_DEAD || state == AI_DOMAIN_ACTIVE) return false;

    if (ai_atomic_load_u64(&domain->memory_used_bytes) != 0 ||
        ai_atomic_load_u32(&domain->tensor_count) != 0 ||
        ai_atomic_load_u32(&domain->work_node_count) != 0 ||
        ai_handle_live_count(domain) != 0 ||
        ai_mapping_live_count(domain) != 0) {
        return false;
    }

    ai_spin_lock(&registry_lock);
    if (ai_domain_state_get(domain) == AI_DOMAIN_ACTIVE ||
        ai_domain_state_get(domain) == AI_DOMAIN_DEAD) {
        ai_spin_unlock(&registry_lock);
        return false;
    }

    for (u32 i = 0; i < AI_MAX_DOMAINS; ++i) {
        if (registry[i] == domain) {
            registry[i] = NULL;
            ai_atomic_store_u32(
                (volatile u32 *)&domain->state,
                (u32)AI_DOMAIN_DEAD);
            if (domain_count == 0) {
                ai_spin_unlock(&registry_lock);
                panic("domain registry underflow");
            }
            domain_count--;
            ai_spin_unlock(&registry_lock);
            return true;
        }
    }

    ai_spin_unlock(&registry_lock);
    return false;
}

bool ai_domain_charge_memory(ai_domain *domain, u64 bytes) {
    if (!domain || ai_domain_state_get(domain) != AI_DOMAIN_ACTIVE) return false;

    u64 current = ai_atomic_load_u64(&domain->memory_used_bytes);
    for (;;) {
        if (domain->limits.memory_bytes != 0) {
            if (current > domain->limits.memory_bytes ||
                bytes > domain->limits.memory_bytes - current) {
                return false;
            }
        }
        if (current > ~0ull - bytes) return false;
        u64 expected = current;
        if (ai_atomic_compare_exchange_u64(
                &domain->memory_used_bytes,
                &expected,
                current + bytes)) {
            return true;
        }
        current = expected;
        if (ai_domain_state_get(domain) != AI_DOMAIN_ACTIVE) return false;
    }
}

void ai_domain_uncharge_memory(ai_domain *domain, u64 bytes) {
    if (!domain_is_live(domain)) return;
    u64 current = ai_atomic_load_u64(&domain->memory_used_bytes);
    for (;;) {
        if (bytes > current) panic("domain memory accounting underflow");
        u64 expected = current;
        if (ai_atomic_compare_exchange_u64(
                &domain->memory_used_bytes,
                &expected,
                current - bytes)) {
            return;
        }
        current = expected;
    }
}

static bool reserve_counter(
    ai_domain *domain,
    volatile u32 *counter,
    u32 limit
) {
    if (!domain || ai_domain_state_get(domain) != AI_DOMAIN_ACTIVE) return false;
    u32 current = ai_atomic_load_u32(counter);
    for (;;) {
        if (limit != 0 && current >= limit) return false;
        if (current == 0xffffffffu) return false;
        u32 expected = current;
        if (ai_atomic_compare_exchange_u32(counter, &expected, current + 1u)) {
            return true;
        }
        current = expected;
        if (ai_domain_state_get(domain) != AI_DOMAIN_ACTIVE) return false;
    }
}

static void release_counter(
    ai_domain *domain,
    volatile u32 *counter,
    const char *underflow_message
) {
    if (!domain_is_live(domain)) return;
    u32 current = ai_atomic_load_u32(counter);
    for (;;) {
        if (current == 0) panic(underflow_message);
        u32 expected = current;
        if (ai_atomic_compare_exchange_u32(counter, &expected, current - 1u)) {
            return;
        }
        current = expected;
    }
}

bool ai_domain_reserve_tensor(ai_domain *domain) {
    if (!domain) return false;
    return reserve_counter(domain, &domain->tensor_count, domain->limits.max_tensors);
}

void ai_domain_release_tensor(ai_domain *domain) {
    if (!domain) return;
    release_counter(domain, &domain->tensor_count, "domain tensor accounting underflow");
}

bool ai_domain_reserve_work_node(ai_domain *domain) {
    if (!domain) return false;
    return reserve_counter(
        domain,
        &domain->work_node_count,
        domain->limits.max_work_nodes);
}

void ai_domain_release_work_node(ai_domain *domain) {
    if (!domain) return;
    release_counter(domain, &domain->work_node_count, "domain work accounting underflow");
}

u64 ai_domain_count(void) {
    ai_spin_lock(&registry_lock);
    u64 result = domain_count;
    ai_spin_unlock(&registry_lock);
    return result;
}

ai_domain_state ai_domain_state_get(const ai_domain *domain) {
    if (!domain) return AI_DOMAIN_DEAD;
    return (ai_domain_state)ai_atomic_load_u32((const volatile u32 *)&domain->state);
}

const char *ai_domain_state_name(ai_domain_state state) {
    switch (state) {
        case AI_DOMAIN_NEW: return "NEW";
        case AI_DOMAIN_ACTIVE: return "ACTIVE";
        case AI_DOMAIN_QUIESCING: return "QUIESCING";
        case AI_DOMAIN_DEAD: return "DEAD";
        default: return "UNKNOWN";
    }
}
