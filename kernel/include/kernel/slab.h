#ifndef AIKERNEL_SLAB_H
#define AIKERNEL_SLAB_H

#include <kernel/types.h>

typedef struct kmem_cache kmem_cache_t;

void slab_init(void);

kmem_cache_t *kmem_cache_create(const char *name, usize obj_size, usize alignment);
void *kmem_cache_alloc(kmem_cache_t *cache);
void kmem_cache_free(kmem_cache_t *cache, void *obj);
void kmem_cache_destroy(kmem_cache_t *cache);

usize kmem_cache_allocated_objects(const kmem_cache_t *cache);
usize kmem_cache_total_frames(const kmem_cache_t *cache);
usize kmem_cache_high_watermark(const kmem_cache_t *cache);

extern kmem_cache_t *ai_tensor_cache;
extern kmem_cache_t *ai_work_node_cache;

#endif
