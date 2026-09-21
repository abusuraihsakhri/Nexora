#ifndef NEXORA_KERNEL_OBJECT_H
#define NEXORA_KERNEL_OBJECT_H

#include <kernel/types.h>

#define NX_OBJECT_REGISTRY_CAPACITY 1024u
#define NX_INVALID_HANDLE ((nx_handle_t)0)
#define NX_OWNER_KERNEL ((nx_owner_id_t)0)
#define NX_OWNER_NONE ((nx_owner_id_t)~(u64)0)

typedef u64 nx_object_id_t;
typedef u64 nx_handle_t;
typedef u64 nx_owner_id_t;

typedef enum {
    NX_OBJECT_INVALID = 0,
    NX_OBJECT_TENSOR,
    NX_OBJECT_WORK,
    NX_OBJECT_CAPABILITY,
    NX_OBJECT_MEMORY,
    NX_OBJECT_DOMAIN,
    NX_OBJECT_DEVICE,
    NX_OBJECT_TYPE_COUNT
} nx_object_type;

typedef enum {
    NX_OBJECT_NEW = 0,
    NX_OBJECT_LIVE,
    NX_OBJECT_QUIESCING,
    NX_OBJECT_DEAD
} nx_object_state;

typedef enum {
    NX_OBJECT_FLAG_NONE       = 0,
    NX_OBJECT_FLAG_KERNEL     = 1u << 0,
    NX_OBJECT_FLAG_PERSISTENT = 1u << 1
} nx_object_flags;

struct nx_object;
typedef void (*nx_object_destroy_fn)(struct nx_object *object);

typedef struct nx_object {
    nx_object_id_t id;
    nx_handle_t handle;
    const char *name;
    nx_object_type type;
    nx_object_state state;
    u32 flags;

    /* Phase 3 Step 4: explicit ownership and lifetime accounting. */
    nx_owner_id_t owner_id;
    u32 strong_refs;
    u32 pin_count;
    bool destroy_invoked;
    nx_object_destroy_fn destroy;
} nx_object;

typedef struct {
    u64 live;
    u64 high_watermark;
    u64 registrations;
    u64 retirements;
    u64 destructions;
    u64 lookup_failures;
    u64 retains;
    u64 releases;
    u64 pins;
    u64 unpins;
    u64 owner_transfers;
} nx_object_stats;

void nx_object_system_init(void);

bool nx_object_register(
    nx_object *object,
    nx_object_type type,
    const char *name,
    u32 flags
);

bool nx_object_register_ex(
    nx_object *object,
    nx_object_type type,
    const char *name,
    u32 flags,
    nx_owner_id_t owner_id,
    nx_object_destroy_fn destroy
);

nx_object *nx_object_lookup(nx_handle_t handle, nx_object_type expected_type);

bool nx_object_retain(nx_handle_t handle);
bool nx_object_release(nx_handle_t handle);
bool nx_object_pin(nx_handle_t handle);
bool nx_object_unpin(nx_handle_t handle);

bool nx_object_transfer_owner(
    nx_handle_t handle,
    nx_owner_id_t expected_owner,
    nx_owner_id_t new_owner
);

u32 nx_object_ref_count(const nx_object *object);
u32 nx_object_pin_count(const nx_object *object);

bool nx_object_transition(nx_object *object, nx_object_state next_state);
bool nx_object_retire(nx_object *object);

u64 nx_object_count(void);
const nx_object_stats *nx_object_get_stats(void);

const char *nx_object_type_name(nx_object_type type);
const char *nx_object_state_name(nx_object_state state);

#endif
