#include <nexora/handle.h>
#include <stddef.h>

#define HANDLE_INDEX_MASK 0xFFFFull
#define HANDLE_GENERATION_SHIFT 16u
#define HANDLE_TYPE_SHIFT 32u

static nexora_handle_t encode_handle(uint16_t index, uint16_t generation, uint8_t type) {
    return ((nexora_handle_t)type << HANDLE_TYPE_SHIFT) |
           ((nexora_handle_t)generation << HANDLE_GENERATION_SHIFT) |
           ((nexora_handle_t)index + 1ull);
}

static nexora_status_t decode_handle(nexora_handle_t handle,
                                     uint16_t *index,
                                     uint16_t *generation,
                                     uint8_t *type) {
    if ((handle >> 40u) != 0) return NEXORA_ERR(NEXORA_EINVAL);
    uint64_t encoded_index = handle & HANDLE_INDEX_MASK;
    if (encoded_index == 0 || encoded_index > NEXORA_MAX_HANDLES) {
        return NEXORA_ERR(NEXORA_EINVAL);
    }
    *index = (uint16_t)(encoded_index - 1u);
    *generation = (uint16_t)((handle >> HANDLE_GENERATION_SHIFT) & 0xFFFFu);
    *type = (uint8_t)((handle >> HANDLE_TYPE_SHIFT) & 0xFFu);
    if (*type == NEXORA_HANDLE_NONE) {
        return NEXORA_ERR(NEXORA_EINVAL);
    }
    return NEXORA_OK;
}

void nexora_handle_table_init(struct nexora_handle_table *table) {
    if (!table) return;
    for (uint32_t i = 0; i < NEXORA_MAX_HANDLES; ++i) {
        table->slots[i].object = NULL;
        table->slots[i].rights = 0;
        table->slots[i].generation = 1;
        table->slots[i].type = NEXORA_HANDLE_NONE;
        table->slots[i].occupied = 0;
    }
}

nexora_status_t nexora_handle_alloc(struct nexora_handle_table *table,
                                    uint8_t type,
                                    void *object,
                                    uint64_t rights,
                                    nexora_handle_t *out) {
    if (!table || !object || !out || type == NEXORA_HANDLE_NONE) {
        return NEXORA_ERR(NEXORA_EINVAL);
    }
    for (uint16_t i = 0; i < NEXORA_MAX_HANDLES; ++i) {
        struct nexora_handle_slot *slot = &table->slots[i];
        if (slot->occupied) continue;
        if (slot->generation == 0) slot->generation = 1;
        slot->occupied = 1;
        slot->type = type;
        slot->object = object;
        slot->rights = rights;
        *out = encode_handle(i, slot->generation, type);
        return NEXORA_OK;
    }
    return NEXORA_ERR(NEXORA_ENOSPC);
}

nexora_status_t nexora_handle_resolve(struct nexora_handle_table *table,
                                      nexora_handle_t handle,
                                      uint8_t expected_type,
                                      uint64_t required_rights,
                                      void **object_out,
                                      uint64_t *rights_out) {
    if (!table) return NEXORA_ERR(NEXORA_EINVAL);
    uint16_t index, generation;
    uint8_t type;
    nexora_status_t status = decode_handle(handle, &index, &generation, &type);
    if (status != NEXORA_OK) return status;
    struct nexora_handle_slot *slot = &table->slots[index];
    if (!slot->occupied || slot->generation != generation) {
        return NEXORA_ERR(NEXORA_ESTALE);
    }
    if (slot->type != type || (expected_type != NEXORA_HANDLE_NONE && slot->type != expected_type)) {
        return NEXORA_ERR(NEXORA_EINVAL);
    }
    if ((slot->rights & required_rights) != required_rights) {
        return NEXORA_ERR(NEXORA_EACCES);
    }
    if (object_out) *object_out = slot->object;
    if (rights_out) *rights_out = slot->rights;
    return NEXORA_OK;
}

nexora_status_t nexora_handle_remove(struct nexora_handle_table *table,
                                     nexora_handle_t handle,
                                     uint8_t expected_type,
                                     void **object_out) {
    if (!table) return NEXORA_ERR(NEXORA_EINVAL);
    uint16_t index, generation;
    uint8_t type;
    nexora_status_t status = decode_handle(handle, &index, &generation, &type);
    if (status != NEXORA_OK) return status;
    struct nexora_handle_slot *slot = &table->slots[index];
    if (!slot->occupied || slot->generation != generation) {
        return NEXORA_ERR(NEXORA_ESTALE);
    }
    if (slot->type != type || (expected_type != NEXORA_HANDLE_NONE && slot->type != expected_type)) {
        return NEXORA_ERR(NEXORA_EINVAL);
    }
    if (object_out) *object_out = slot->object;
    slot->object = NULL;
    slot->rights = 0;
    slot->type = NEXORA_HANDLE_NONE;
    slot->occupied = 0;
    slot->generation = (uint16_t)(slot->generation + 1u);
    if (slot->generation == 0) slot->generation = 1;
    return NEXORA_OK;
}
