#include <kernel/slab.h>
#include <kernel/frame.h>
#include <ai/tensor.h>
#include <ai/work.h>

#define MAX_CACHES 8

struct kmem_cache {
    const char *name;
    usize obj_size;
    usize alignment;
    usize objs_per_slab;
    void *free_list;
    usize total_objs;
    usize allocated_objs;
    usize total_frames;
    usize high_watermark;
    bool active;
};

static kmem_cache_t s_caches[MAX_CACHES];
static usize s_cache_count = 0;

kmem_cache_t *ai_tensor_cache = NULL;
kmem_cache_t *ai_work_node_cache = NULL;

kmem_cache_t *kmem_cache_create(const char *name, usize obj_size, usize alignment) {
    if (s_cache_count >= MAX_CACHES) return NULL;
    if (obj_size == 0) return NULL;
    if (alignment < sizeof(void *)) alignment = sizeof(void *);

    /* Align object size */
    usize aligned_size = (obj_size + alignment - 1) & ~(alignment - 1);
    if (aligned_size < sizeof(void *)) {
        aligned_size = sizeof(void *);
    }
    if (aligned_size > PAGE_SIZE) {
        return NULL; /* Slabs only support single-page object sizes */
    }

    kmem_cache_t *c = &s_caches[s_cache_count++];
    c->name = name;
    c->obj_size = aligned_size;
    c->alignment = alignment;
    c->objs_per_slab = PAGE_SIZE / aligned_size;
    c->free_list = NULL;
    c->total_objs = 0;
    c->allocated_objs = 0;
    c->total_frames = 0;
    c->high_watermark = 0;
    c->active = true;

    return c;
}

void *kmem_cache_alloc(kmem_cache_t *cache) {
    if (!cache || !cache->active) return NULL;

    if (!cache->free_list) {
        /* Allocate a physical frame from frame allocator */
        uintptr_t paddr = frame_alloc();
        if (!paddr) return NULL;

        cache->total_frames++;
        void *page = (void *)paddr;

        /* Carve page into objects and push onto free list */
        for (usize i = 0; i < cache->objs_per_slab; ++i) {
            void *chunk = (void *)((uintptr_t)page + i * cache->obj_size);
            *(void **)chunk = cache->free_list;
            cache->free_list = chunk;
            cache->total_objs++;
        }
    }

    /* Pop from free list */
    void *obj = cache->free_list;
    cache->free_list = *(void **)obj;
    cache->allocated_objs++;

    if (cache->allocated_objs > cache->high_watermark) {
        cache->high_watermark = cache->allocated_objs;
    }

    /* Zero out object memory */
    u8 *bytes = (u8 *)obj;
    for (usize i = 0; i < cache->obj_size; ++i) {
        bytes[i] = 0;
    }

    return obj;
}

void kmem_cache_free(kmem_cache_t *cache, void *obj) {
    if (!cache || !cache->active || !obj) return;

    /* Push back onto free list */
    *(void **)obj = cache->free_list;
    cache->free_list = obj;

    if (cache->allocated_objs > 0) {
        cache->allocated_objs--;
    }
}

void kmem_cache_destroy(kmem_cache_t *cache) {
    if (!cache) return;
    cache->active = false;
    cache->free_list = NULL;
    cache->allocated_objs = 0;
}

usize kmem_cache_allocated_objects(const kmem_cache_t *cache) {
    return cache ? cache->allocated_objs : 0;
}

usize kmem_cache_total_frames(const kmem_cache_t *cache) {
    return cache ? cache->total_frames : 0;
}

usize kmem_cache_high_watermark(const kmem_cache_t *cache) {
    return cache ? cache->high_watermark : 0;
}

void slab_init(void) {
    s_cache_count = 0;
    for (usize i = 0; i < MAX_CACHES; ++i) {
        s_caches[i].active = false;
        s_caches[i].free_list = NULL;
    }
    ai_tensor_cache = kmem_cache_create("ai_tensor", sizeof(ai_tensor), 16);
    ai_work_node_cache = kmem_cache_create("ai_work_node", sizeof(ai_work_node), 16);
}
