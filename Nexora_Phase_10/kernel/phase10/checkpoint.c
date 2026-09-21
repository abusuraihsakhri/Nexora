#include "nexora/phase10/checkpoint.h"

static uint64_t mix64(uint64_t x)
{
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

uint64_t nx_checkpoint_integrity_tag(const nx_checkpoint_t *checkpoint)
{
    uint64_t h;
    if (!checkpoint) return 0;
    h = 0x4e45584f52415031ULL; /* "NEXORAP1" marker */
    h ^= mix64(checkpoint->checkpoint_id);
    h ^= mix64((uint64_t)checkpoint->owner_id << 1);
    h ^= mix64(checkpoint->generation << 7);
    h ^= mix64(checkpoint->payload_tag << 13);
    h ^= mix64(checkpoint->created_at << 19);
    return mix64(h);
}

void nx_checkpoint_init(nx_checkpoint_store_t *store)
{
    if (!store) return;
    nx_p10_memzero(store, sizeof(*store));
    store->next_id = 1;
}

static nx_checkpoint_t *find_ckpt(nx_checkpoint_store_t *store, uint64_t id)
{
    size_t i;
    if (!store || id == 0) return NULL;
    for (i = 0; i < NX_P10_MAX_CHECKPOINTS; ++i)
        if (store->slots[i].state != NX_CKPT_EMPTY &&
            store->slots[i].checkpoint_id == id)
            return &store->slots[i];
    return NULL;
}

nx_p10_status_t nx_checkpoint_prepare(nx_checkpoint_store_t *store,
                                       nx_p10_id_t owner_id,
                                       uint64_t generation,
                                       uint64_t payload_tag,
                                       nx_p10_time_t now,
                                       uint64_t *out_checkpoint_id)
{
    size_t i, oldest_committed = NX_P10_MAX_CHECKPOINTS;
    nx_checkpoint_t *slot = NULL;
    if (!store || !out_checkpoint_id || owner_id == 0) return NX_P10_EINVAL;
    for (i = 0; i < NX_P10_MAX_CHECKPOINTS; ++i) {
        if (store->slots[i].state == NX_CKPT_EMPTY ||
            store->slots[i].state == NX_CKPT_INVALID) {
            slot = &store->slots[i];
            break;
        }
        if (store->slots[i].state == NX_CKPT_COMMITTED &&
            (oldest_committed == NX_P10_MAX_CHECKPOINTS ||
             store->slots[i].created_at < store->slots[oldest_committed].created_at))
            oldest_committed = i;
    }
    /* Never evict a PREPARED checkpoint: preserve prepare -> commit atomicity. */
    if (!slot && oldest_committed != NX_P10_MAX_CHECKPOINTS)
        slot = &store->slots[oldest_committed];
    if (!slot)
        return NX_P10_ENOSPC;

    nx_p10_memzero(slot, sizeof(*slot));
    slot->checkpoint_id = store->next_id++;
    if (slot->checkpoint_id == 0u) {
        slot->checkpoint_id = store->next_id++;
        if (slot->checkpoint_id == 0u)
            return NX_P10_ESTATE;
    }
    slot->owner_id = owner_id;
    slot->generation = generation;
    slot->payload_tag = payload_tag;
    slot->created_at = now;
    slot->state = NX_CKPT_PREPARED;
    slot->integrity_tag = nx_checkpoint_integrity_tag(slot);
    *out_checkpoint_id = slot->checkpoint_id;
    return NX_P10_OK;
}

nx_p10_status_t nx_checkpoint_commit(nx_checkpoint_store_t *store,
                                      uint64_t checkpoint_id)
{
    nx_checkpoint_t *ckpt = find_ckpt(store, checkpoint_id);
    if (!ckpt) return NX_P10_ENOENT;
    if (ckpt->state != NX_CKPT_PREPARED) return NX_P10_ESTATE;
    if (!nx_checkpoint_verify(ckpt)) {
        ckpt->state = NX_CKPT_INVALID;
        return NX_P10_ECORRUPT;
    }
    ckpt->state = NX_CKPT_COMMITTED;
    return NX_P10_OK;
}

nx_p10_status_t nx_checkpoint_invalidate(nx_checkpoint_store_t *store,
                                          uint64_t checkpoint_id)
{
    nx_checkpoint_t *ckpt = find_ckpt(store, checkpoint_id);
    if (!ckpt) return NX_P10_ENOENT;
    ckpt->state = NX_CKPT_INVALID;
    return NX_P10_OK;
}

int nx_checkpoint_verify(const nx_checkpoint_t *checkpoint)
{
    if (!checkpoint || checkpoint->state == NX_CKPT_EMPTY ||
        checkpoint->state == NX_CKPT_INVALID)
        return 0;
    return checkpoint->integrity_tag == nx_checkpoint_integrity_tag(checkpoint);
}

const nx_checkpoint_t *nx_checkpoint_latest(const nx_checkpoint_store_t *store,
                                            nx_p10_id_t owner_id)
{
    const nx_checkpoint_t *best = NULL;
    size_t i;
    if (!store || owner_id == 0) return NULL;
    for (i = 0; i < NX_P10_MAX_CHECKPOINTS; ++i) {
        const nx_checkpoint_t *c = &store->slots[i];
        if (c->state != NX_CKPT_COMMITTED || c->owner_id != owner_id ||
            !nx_checkpoint_verify(c))
            continue;
        if (!best || c->generation > best->generation ||
            (c->generation == best->generation && c->created_at > best->created_at))
            best = c;
    }
    return best;
}

const nx_checkpoint_t *nx_checkpoint_get_committed(const nx_checkpoint_store_t *store,
                                                   uint64_t checkpoint_id,
                                                   nx_p10_id_t owner_id)
{
    size_t i;
    if (!store || checkpoint_id == 0u || owner_id == 0u) return NULL;
    for (i = 0; i < NX_P10_MAX_CHECKPOINTS; ++i) {
        const nx_checkpoint_t *c = &store->slots[i];
        if (c->checkpoint_id == checkpoint_id && c->owner_id == owner_id &&
            c->state == NX_CKPT_COMMITTED && nx_checkpoint_verify(c))
            return c;
    }
    return NULL;
}
