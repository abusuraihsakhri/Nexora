#include <kernel/object.h>

typedef struct {
    nx_object *object;
    u32 generation;
} nx_object_slot;

static nx_object_slot registry[NX_OBJECT_REGISTRY_CAPACITY];
static nx_object_id_t next_object_id;
static nx_object_stats stats;

static nx_handle_t make_handle(u32 slot, u32 generation) {
    /* Low 32 bits store slot + 1 so handle 0 can remain permanently invalid. */
    return ((u64)generation << 32) | (u64)(slot + 1u);
}

static bool decode_handle(nx_handle_t handle, u32 *slot, u32 *generation) {
    if (handle == NX_INVALID_HANDLE || !slot || !generation) {
        return false;
    }

    u32 encoded_slot = (u32)(handle & 0xffffffffu);
    if (encoded_slot == 0) {
        return false;
    }

    *slot = encoded_slot - 1u;
    *generation = (u32)(handle >> 32);

    if (*slot >= NX_OBJECT_REGISTRY_CAPACITY || *generation == 0) {
        return false;
    }

    return true;
}

static bool transition_allowed(nx_object_state current, nx_object_state next) {
    if (current == next) {
        return true;
    }

    switch (current) {
        case NX_OBJECT_NEW:
            return next == NX_OBJECT_LIVE || next == NX_OBJECT_DEAD;
        case NX_OBJECT_LIVE:
            return next == NX_OBJECT_QUIESCING || next == NX_OBJECT_DEAD;
        case NX_OBJECT_QUIESCING:
            return next == NX_OBJECT_DEAD;
        case NX_OBJECT_DEAD:
        default:
            return false;
    }
}

static bool finalize_object(nx_object *object) {
    if (!object || object->handle == NX_INVALID_HANDLE ||
        object->state == NX_OBJECT_DEAD || object->strong_refs != 0 ||
        object->pin_count != 0) {
        return false;
    }

    u32 slot_index;
    u32 generation;
    if (!decode_handle(object->handle, &slot_index, &generation)) {
        return false;
    }

    nx_object_slot *slot = &registry[slot_index];
    if (slot->object != object || slot->generation != generation) {
        return false;
    }

    if (object->state == NX_OBJECT_LIVE &&
        !nx_object_transition(object, NX_OBJECT_QUIESCING)) {
        return false;
    }

    if (!object->destroy_invoked && object->destroy != NULL) {
        object->destroy_invoked = true;
        object->destroy(object);
        ++stats.destructions;
    }

    if (!nx_object_transition(object, NX_OBJECT_DEAD)) {
        return false;
    }

    slot->object = NULL;
    ++slot->generation;
    if (slot->generation == 0) {
        slot->generation = 1;
    }

    object->handle = NX_INVALID_HANDLE;

    if (stats.live > 0) {
        --stats.live;
    }
    ++stats.retirements;
    return true;
}

void nx_object_system_init(void) {
    for (u32 i = 0; i < NX_OBJECT_REGISTRY_CAPACITY; ++i) {
        registry[i].object = NULL;
        registry[i].generation = 1;
    }

    next_object_id = 1;
    stats.live = 0;
    stats.high_watermark = 0;
    stats.registrations = 0;
    stats.retirements = 0;
    stats.destructions = 0;
    stats.lookup_failures = 0;
    stats.retains = 0;
    stats.releases = 0;
    stats.pins = 0;
    stats.unpins = 0;
    stats.owner_transfers = 0;
}

bool nx_object_register(
    nx_object *object,
    nx_object_type type,
    const char *name,
    u32 flags
) {
    return nx_object_register_ex(
        object,
        type,
        name,
        flags,
        NX_OWNER_KERNEL,
        NULL
    );
}

bool nx_object_register_ex(
    nx_object *object,
    nx_object_type type,
    const char *name,
    u32 flags,
    nx_owner_id_t owner_id,
    nx_object_destroy_fn destroy
) {
    if (!object || type <= NX_OBJECT_INVALID || type >= NX_OBJECT_TYPE_COUNT ||
        owner_id == NX_OWNER_NONE) {
        return false;
    }

    u32 free_slot = NX_OBJECT_REGISTRY_CAPACITY;
    for (u32 i = 0; i < NX_OBJECT_REGISTRY_CAPACITY; ++i) {
        if (!registry[i].object) {
            free_slot = i;
            break;
        }
    }

    if (free_slot == NX_OBJECT_REGISTRY_CAPACITY) {
        return false;
    }

    nx_object_slot *slot = &registry[free_slot];
    if (slot->generation == 0) {
        slot->generation = 1;
    }

    object->id = next_object_id++;
    object->handle = make_handle(free_slot, slot->generation);
    object->name = name;
    object->type = type;
    object->state = NX_OBJECT_NEW;
    object->flags = flags;
    object->owner_id = owner_id;
    object->strong_refs = 1; /* creator/owner reference */
    object->pin_count = 0;
    object->destroy_invoked = false;
    object->destroy = destroy;

    slot->object = object;

    ++stats.live;
    ++stats.registrations;
    if (stats.live > stats.high_watermark) {
        stats.high_watermark = stats.live;
    }

    return nx_object_transition(object, NX_OBJECT_LIVE);
}

nx_object *nx_object_lookup(nx_handle_t handle, nx_object_type expected_type) {
    u32 slot_index;
    u32 generation;

    if (!decode_handle(handle, &slot_index, &generation)) {
        ++stats.lookup_failures;
        return NULL;
    }

    nx_object_slot *slot = &registry[slot_index];
    nx_object *object = slot->object;

    if (!object || slot->generation != generation ||
        object->handle != handle || object->state == NX_OBJECT_DEAD) {
        ++stats.lookup_failures;
        return NULL;
    }

    if (expected_type != NX_OBJECT_INVALID && object->type != expected_type) {
        ++stats.lookup_failures;
        return NULL;
    }

    return object;
}

bool nx_object_retain(nx_handle_t handle) {
    nx_object *object = nx_object_lookup(handle, NX_OBJECT_INVALID);
    if (!object || object->state != NX_OBJECT_LIVE || object->strong_refs == 0 ||
        object->strong_refs == ~(u32)0) {
        return false;
    }

    ++object->strong_refs;
    ++stats.retains;
    return true;
}

bool nx_object_release(nx_handle_t handle) {
    nx_object *object = nx_object_lookup(handle, NX_OBJECT_INVALID);
    if (!object || object->strong_refs == 0) {
        return false;
    }

    --object->strong_refs;
    ++stats.releases;

    if (object->strong_refs != 0) {
        return true;
    }

    if (object->state == NX_OBJECT_LIVE &&
        !nx_object_transition(object, NX_OBJECT_QUIESCING)) {
        return false;
    }

    if (object->pin_count != 0) {
        return true;
    }

    return finalize_object(object);
}

bool nx_object_pin(nx_handle_t handle) {
    nx_object *object = nx_object_lookup(handle, NX_OBJECT_INVALID);
    if (!object || object->state != NX_OBJECT_LIVE || object->strong_refs == 0 ||
        object->pin_count == ~(u32)0) {
        return false;
    }

    ++object->pin_count;
    ++stats.pins;
    return true;
}

bool nx_object_unpin(nx_handle_t handle) {
    nx_object *object = nx_object_lookup(handle, NX_OBJECT_INVALID);
    if (!object || object->pin_count == 0) {
        return false;
    }

    --object->pin_count;
    ++stats.unpins;

    if (object->pin_count == 0 && object->strong_refs == 0) {
        return finalize_object(object);
    }

    return true;
}

bool nx_object_transfer_owner(
    nx_handle_t handle,
    nx_owner_id_t expected_owner,
    nx_owner_id_t new_owner
) {
    nx_object *object = nx_object_lookup(handle, NX_OBJECT_INVALID);
    if (!object || object->state != NX_OBJECT_LIVE ||
        new_owner == NX_OWNER_NONE || object->owner_id != expected_owner) {
        return false;
    }

    object->owner_id = new_owner;
    ++stats.owner_transfers;
    return true;
}

u32 nx_object_ref_count(const nx_object *object) {
    return object ? object->strong_refs : 0;
}

u32 nx_object_pin_count(const nx_object *object) {
    return object ? object->pin_count : 0;
}

bool nx_object_transition(nx_object *object, nx_object_state next_state) {
    if (!object || !transition_allowed(object->state, next_state)) {
        return false;
    }

    object->state = next_state;
    return true;
}

bool nx_object_retire(nx_object *object) {
    if (!object || object->handle == NX_INVALID_HANDLE ||
        object->state == NX_OBJECT_DEAD || object->strong_refs != 1 ||
        object->pin_count != 0) {
        return false;
    }

    /* Compatibility helper: retire consumes the sole creator reference. */
    object->strong_refs = 0;
    ++stats.releases;
    return finalize_object(object);
}

u64 nx_object_count(void) {
    return stats.live;
}

const nx_object_stats *nx_object_get_stats(void) {
    return &stats;
}

const char *nx_object_type_name(nx_object_type type) {
    switch (type) {
        case NX_OBJECT_TENSOR: return "TENSOR";
        case NX_OBJECT_WORK: return "WORK";
        case NX_OBJECT_CAPABILITY: return "CAPABILITY";
        case NX_OBJECT_MEMORY: return "MEMORY";
        case NX_OBJECT_DOMAIN: return "DOMAIN";
        case NX_OBJECT_DEVICE: return "DEVICE";
        case NX_OBJECT_INVALID:
        default: return "INVALID";
    }
}

const char *nx_object_state_name(nx_object_state state) {
    switch (state) {
        case NX_OBJECT_NEW: return "NEW";
        case NX_OBJECT_LIVE: return "LIVE";
        case NX_OBJECT_QUIESCING: return "QUIESCING";
        case NX_OBJECT_DEAD: return "DEAD";
        default: return "UNKNOWN";
    }
}
