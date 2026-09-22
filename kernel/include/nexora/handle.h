#ifndef NEXORA_HANDLE_H
#define NEXORA_HANDLE_H

#include <stdint.h>
#include <stdbool.h>
#include <nexora/abi.h>

#define NEXORA_MAX_HANDLES 128u

enum nexora_handle_type {
    NEXORA_HANDLE_NONE = 0,
    NEXORA_HANDLE_TENSOR = 1,
    NEXORA_HANDLE_WORK = 2,
    NEXORA_HANDLE_CAPABILITY = 3,
};

struct nexora_handle_slot {
    void *object;
    uint64_t rights;
    uint16_t generation;
    uint8_t type;
    uint8_t occupied;
};

struct nexora_handle_table {
    struct nexora_handle_slot slots[NEXORA_MAX_HANDLES];
};

void nexora_handle_table_init(struct nexora_handle_table *table);
nexora_status_t nexora_handle_alloc(struct nexora_handle_table *table,
                                    uint8_t type,
                                    void *object,
                                    uint64_t rights,
                                    nexora_handle_t *out);
nexora_status_t nexora_handle_resolve(struct nexora_handle_table *table,
                                      nexora_handle_t handle,
                                      uint8_t expected_type,
                                      uint64_t required_rights,
                                      void **object_out,
                                      uint64_t *rights_out);
nexora_status_t nexora_handle_remove(struct nexora_handle_table *table,
                                     nexora_handle_t handle,
                                     uint8_t expected_type,
                                     void **object_out);

#endif
