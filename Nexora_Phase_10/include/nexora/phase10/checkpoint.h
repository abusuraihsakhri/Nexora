#ifndef NEXORA_PHASE10_CHECKPOINT_H
#define NEXORA_PHASE10_CHECKPOINT_H

#include "p10_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nx_checkpoint_state {
    NX_CKPT_EMPTY = 0,
    NX_CKPT_PREPARED,
    NX_CKPT_COMMITTED,
    NX_CKPT_INVALID
} nx_checkpoint_state_t;

typedef struct nx_checkpoint {
    uint64_t checkpoint_id;
    nx_p10_id_t owner_id;
    uint64_t generation;
    uint64_t payload_tag;
    uint64_t integrity_tag;
    nx_p10_time_t created_at;
    nx_checkpoint_state_t state;
} nx_checkpoint_t;

typedef struct nx_checkpoint_store {
    nx_checkpoint_t slots[NX_P10_MAX_CHECKPOINTS];
    uint64_t next_id;
} nx_checkpoint_store_t;

void nx_checkpoint_init(nx_checkpoint_store_t *store);
nx_p10_status_t nx_checkpoint_prepare(nx_checkpoint_store_t *store,
                                       nx_p10_id_t owner_id,
                                       uint64_t generation,
                                       uint64_t payload_tag,
                                       nx_p10_time_t now,
                                       uint64_t *out_checkpoint_id);
nx_p10_status_t nx_checkpoint_commit(nx_checkpoint_store_t *store,
                                      uint64_t checkpoint_id);
nx_p10_status_t nx_checkpoint_invalidate(nx_checkpoint_store_t *store,
                                          uint64_t checkpoint_id);
const nx_checkpoint_t *nx_checkpoint_latest(const nx_checkpoint_store_t *store,
                                            nx_p10_id_t owner_id);
const nx_checkpoint_t *nx_checkpoint_get_committed(const nx_checkpoint_store_t *store,
                                                   uint64_t checkpoint_id,
                                                   nx_p10_id_t owner_id);
int nx_checkpoint_verify(const nx_checkpoint_t *checkpoint);
uint64_t nx_checkpoint_integrity_tag(const nx_checkpoint_t *checkpoint);

#ifdef __cplusplus
}
#endif

#endif
