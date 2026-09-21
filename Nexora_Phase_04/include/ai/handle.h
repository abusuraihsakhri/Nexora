#ifndef AIKERNEL_AI_HANDLE_H
#define AIKERNEL_AI_HANDLE_H

#include <kernel/types.h>
#include <kernel/spinlock.h>

typedef u64 ai_handle_t;
typedef u32 ai_handle_rights_t;

#define AI_HANDLE_INVALID ((ai_handle_t)0)
#define AI_DOMAIN_MAX_HANDLES 64u

typedef enum {
    AI_HANDLE_OBJECT_NONE = 0,
    AI_HANDLE_OBJECT_TENSOR,
    AI_HANDLE_OBJECT_BACKING,
    AI_HANDLE_OBJECT_WORK,
    AI_HANDLE_OBJECT_GENERIC,
    AI_HANDLE_OBJECT_ANY = 0xff
} ai_handle_object_type;

enum {
    AI_HANDLE_RIGHT_READ     = 1u << 0,
    AI_HANDLE_RIGHT_WRITE    = 1u << 1,
    AI_HANDLE_RIGHT_MAP      = 1u << 2,
    AI_HANDLE_RIGHT_SHARE    = 1u << 3,
    AI_HANDLE_RIGHT_TRANSFER = 1u << 4,
    AI_HANDLE_RIGHT_ADMIN    = 1u << 5
};

#define AI_HANDLE_RIGHT_ALL ((ai_handle_rights_t)( \
    AI_HANDLE_RIGHT_READ     | \
    AI_HANDLE_RIGHT_WRITE    | \
    AI_HANDLE_RIGHT_MAP      | \
    AI_HANDLE_RIGHT_SHARE    | \
    AI_HANDLE_RIGHT_TRANSFER | \
    AI_HANDLE_RIGHT_ADMIN))

typedef struct {
    void *object;
    /* Generation 0 is reserved for a permanently exhausted slot. */
    u32 generation;
    ai_handle_object_type type;
    ai_handle_rights_t rights;
    bool occupied;
    bool revoked;
} ai_handle_entry;

typedef struct {
    ai_handle_entry entries[AI_DOMAIN_MAX_HANDLES];
    u32 live_count;
    u32 revoked_count;
    ai_spinlock lock;
} ai_handle_table;

struct ai_domain;

void ai_handle_table_init(ai_handle_table *table);

ai_handle_t ai_handle_install(
    struct ai_domain *domain,
    void *object,
    ai_handle_object_type type,
    ai_handle_rights_t rights
);

/*
 * Non-owning lookup retained for diagnostics and single-threaded callers.
 * Concurrent kernel paths should use ai_handle_acquire(), which pins managed
 * tensor/backing objects until ai_handle_release_object().
 */
void *ai_handle_resolve(
    const struct ai_domain *domain,
    ai_handle_t handle,
    ai_handle_object_type expected_type,
    ai_handle_rights_t required_rights
);

void *ai_handle_acquire(
    const struct ai_domain *domain,
    ai_handle_t handle,
    ai_handle_object_type expected_type,
    ai_handle_rights_t required_rights
);

bool ai_handle_release_object(void *object, ai_handle_object_type type);

bool ai_handle_is_valid(
    const struct ai_domain *domain,
    ai_handle_t handle,
    ai_handle_object_type expected_type,
    ai_handle_rights_t required_rights
);

bool ai_handle_has_rights(
    const struct ai_domain *domain,
    ai_handle_t handle,
    ai_handle_rights_t required_rights
);

ai_handle_rights_t ai_handle_get_rights(
    const struct ai_domain *domain,
    ai_handle_t handle
);

bool ai_handle_restrict_rights(
    struct ai_domain *domain,
    ai_handle_t handle,
    ai_handle_rights_t new_rights
);

ai_handle_t ai_handle_share(
    struct ai_domain *source_domain,
    ai_handle_t source_handle,
    struct ai_domain *target_domain,
    ai_handle_rights_t requested_rights
);

ai_handle_t ai_handle_transfer(
    struct ai_domain *source_domain,
    ai_handle_t source_handle,
    struct ai_domain *target_domain,
    ai_handle_rights_t requested_rights
);

/*
 * Revocation immediately denies future resolution/acquisition while retaining
 * the namespace entry until close/reap. This makes revocation race-safe: an
 * already-acquired managed-object pin remains valid, but no new pin can begin.
 */
bool ai_handle_revoke(struct ai_domain *domain, ai_handle_t handle);
u32 ai_handle_reap_revoked(struct ai_domain *domain);
bool ai_handle_is_revoked(const struct ai_domain *domain, ai_handle_t handle);

bool ai_handle_close(struct ai_domain *domain, ai_handle_t handle);

bool ai_handle_rights_valid(ai_handle_rights_t rights);
u32 ai_handle_live_count(const struct ai_domain *domain);
u32 ai_handle_revoked_count(const struct ai_domain *domain);
u32 ai_handle_domain_tag(ai_handle_t handle);
u32 ai_handle_generation(ai_handle_t handle);
u32 ai_handle_slot(ai_handle_t handle);
const char *ai_handle_object_type_name(ai_handle_object_type type);

#endif
