#ifndef AIKERNEL_AI_DOMAIN_H
#define AIKERNEL_AI_DOMAIN_H

#include <kernel/types.h>
#include <ai/handle.h>
#include <ai/mapping.h>

#define AI_MAX_DOMAINS 64

/* Phase 4 handle tokens store the domain identity in 32 bits. */
#define AI_DOMAIN_ID_MAX 0xffffffffull

typedef u64 ai_domain_id_t;

typedef enum {
    AI_DOMAIN_NEW = 0,
    AI_DOMAIN_ACTIVE,
    AI_DOMAIN_QUIESCING,
    AI_DOMAIN_DEAD
} ai_domain_state;

typedef enum {
    AI_DOMAIN_SYSTEM  = 1u << 0,
    AI_DOMAIN_TRUSTED = 1u << 1
} ai_domain_flags;

/* A zero limit means "unlimited" for the current prototype. */
typedef struct {
    u64 memory_bytes;
    u32 max_tensors;
    u32 max_work_nodes;
} ai_domain_limits;

typedef struct ai_domain {
    ai_domain_id_t id;
    const char *name;
    ai_domain_state state;
    u32 flags;

    ai_domain_limits limits;

    u64 memory_used_bytes;
    u32 tensor_count;
    u32 work_node_count;

    /* Every externally visible object reference is scoped by this table. */
    ai_handle_table handles;

    /* Logical per-domain tensor mappings; hardware VM binding comes later. */
    ai_mapping_table mappings;
} ai_domain;

void ai_domain_system_init(void);

ai_domain *ai_domain_create(
    const char *name,
    u32 flags,
    ai_domain_limits limits
);

ai_domain *ai_domain_lookup(ai_domain_id_t id);

bool ai_domain_activate(ai_domain *domain);
bool ai_domain_begin_quiesce(ai_domain *domain);
bool ai_domain_destroy(ai_domain *domain);

bool ai_domain_charge_memory(ai_domain *domain, u64 bytes);
void ai_domain_uncharge_memory(ai_domain *domain, u64 bytes);

bool ai_domain_reserve_tensor(ai_domain *domain);
void ai_domain_release_tensor(ai_domain *domain);

bool ai_domain_reserve_work_node(ai_domain *domain);
void ai_domain_release_work_node(ai_domain *domain);

u64 ai_domain_count(void);
ai_domain_state ai_domain_state_get(const ai_domain *domain);
const char *ai_domain_state_name(ai_domain_state state);

#endif
