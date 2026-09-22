#include <kernel/slab.h>
#include <kernel/frame.h>
#include <ai/tensor.h>
#include <ai/work.h>

#define MAX_CACHES 8

typedef struct slab_page_header slab_page_header_t;

struct slab_page_header {
    struct kmem_cache *cache;
    slab_page_header_t *next;
    usize object_count;
    usize free_count;
};

struct kmem_cache {
    const char *name;
    usize obj_size;
    usize alignment;
    usize objs_per_slab;
    void *free_list;
    slab_page_header_t *pages;
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

static bool is_power_of_two(usize value) {
    return value != 0 && (value & (value - 1u)) == 0;
}

static uintptr_t align_up_uintptr(uintptr_t value, usize alignment) {
    return (value + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
}

static uintptr_t page_base_of(const void *ptr) {
    return (uintptr_t)ptr & ~(uintptr_t)(PAGE_SIZE - 1u);
}

static void unlink_page_objects(kmem_cache_t *cache, uintptr_t page_base) {
    void **link = &cache->free_list;
    while (*link) {
        void *obj = *link;
        if (page_base_of(obj) == page_base) {
            *link = *(void **)obj;
        } else {
            link = (void **)obj;
        }
    }
}

static void unlink_page_header(kmem_cache_t *cache, slab_page_header_t *page) {
    slab_page_header_t **link = &cache->pages;
    while (*link) {
        if (*link == page) {
            *link = page->next;
            return;
        }
        link = &(*link)->next;
    }
}

static bool add_slab_page(kmem_cache_t *cache) {
    uintptr_t paddr = frame_alloc();
    if (!paddr) return false;

    slab_page_header_t *page = (slab_page_header_t *)paddr;
    uintptr_t object_start = align_up_uintptr(
        paddr + sizeof(slab_page_header_t),
        cache->alignment
    );
    usize overhead = (usize)(object_start - paddr);
    if (overhead >= PAGE_SIZE) {
        frame_free(paddr);
        return false;
    }

    usize object_count = (PAGE_SIZE - overhead) / cache->obj_size;
    if (object_count == 0) {
        frame_free(paddr);
        return false;
    }

    page->cache = cache;
    page->next = cache->pages;
    page->object_count = object_count;
    page->free_count = object_count;
    cache->pages = page;
    cache->total_frames++;
    cache->total_objs += object_count;
    cache->objs_per_slab = object_count;

    for (usize i = 0; i < object_count; ++i) {
        void *chunk = (void *)(object_start + i * cache->obj_size);
        *(void **)chunk = cache->free_list;
        cache->free_list = chunk;
    }
    return true;
}

static void reclaim_page_if_excess(kmem_cache_t *cache, slab_page_header_t *page) {
    if (!cache || !page) return;
    if (page->free_count != page->object_count) return;

    /*
     * Keep one empty slab as a hot spare to avoid frame churn for bursty
     * single-object workloads.  Every additional completely free page is
     * returned to the physical frame allocator.
     */
    if (cache->total_frames <= 1u) return;

    uintptr_t paddr = (uintptr_t)page;
    unlink_page_objects(cache, paddr);
    unlink_page_header(cache, page);
    if (cache->total_objs >= page->object_count) {
        cache->total_objs -= page->object_count;
    } else {
        cache->total_objs = 0;
    }
    cache->total_frames--;
    frame_free(paddr);
}

kmem_cache_t *kmem_cache_create(const char *name, usize obj_size, usize alignment) {
    if (s_cache_count >= MAX_CACHES) return NULL;
    if (!name || obj_size == 0) return NULL;

    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    if (!is_power_of_two(alignment) || alignment > PAGE_SIZE) return NULL;
    if (obj_size > PAGE_SIZE) return NULL;
    if (obj_size > (usize)(~(usize)0) - (alignment - 1u)) return NULL;

    usize aligned_size = (obj_size + alignment - 1u) & ~(alignment - 1u);
    if (aligned_size < sizeof(void *)) aligned_size = sizeof(void *);

    uintptr_t first = align_up_uintptr(
        (uintptr_t)sizeof(slab_page_header_t),
        alignment
    );
    if (first >= PAGE_SIZE || aligned_size > PAGE_SIZE - first) return NULL;

    kmem_cache_t *c = &s_caches[s_cache_count++];
    c->name = name;
    c->obj_size = aligned_size;
    c->alignment = alignment;
    c->objs_per_slab = (PAGE_SIZE - first) / aligned_size;
    c->free_list = NULL;
    c->pages = NULL;
    c->total_objs = 0;
    c->allocated_objs = 0;
    c->total_frames = 0;
    c->high_watermark = 0;
    c->active = true;
    return c;
}

void *kmem_cache_alloc(kmem_cache_t *cache) {
    if (!cache || !cache->active) return NULL;

    if (!cache->free_list && !add_slab_page(cache)) return NULL;

    void *obj = cache->free_list;
    cache->free_list = *(void **)obj;

    slab_page_header_t *page = (slab_page_header_t *)page_base_of(obj);
    if (page->cache != cache || page->free_count == 0) return NULL;
    page->free_count--;

    cache->allocated_objs++;
    if (cache->allocated_objs > cache->high_watermark) {
        cache->high_watermark = cache->allocated_objs;
    }

    u8 *bytes = (u8 *)obj;
    for (usize i = 0; i < cache->obj_size; ++i) bytes[i] = 0;
    return obj;
}

void kmem_cache_free(kmem_cache_t *cache, void *obj) {
    if (!cache || !cache->active || !obj) return;

    slab_page_header_t *page = (slab_page_header_t *)page_base_of(obj);
    if (page->cache != cache || page->free_count >= page->object_count) return;

    *(void **)obj = cache->free_list;
    cache->free_list = obj;
    page->free_count++;

    if (cache->allocated_objs > 0) cache->allocated_objs--;
    reclaim_page_if_excess(cache, page);
}

void kmem_cache_destroy(kmem_cache_t *cache) {
    if (!cache || !cache->active) return;
    if (cache->allocated_objs != 0) return;

    slab_page_header_t *page = cache->pages;
    while (page) {
        slab_page_header_t *next = page->next;
        frame_free((uintptr_t)page);
        page = next;
    }

    cache->active = false;
    cache->free_list = NULL;
    cache->pages = NULL;
    cache->total_objs = 0;
    cache->total_frames = 0;
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
        s_caches[i].pages = NULL;
    }
    ai_tensor_cache = kmem_cache_create("ai_tensor", sizeof(ai_tensor), 16);
    ai_work_node_cache = kmem_cache_create("ai_work_node", sizeof(ai_work_node), 16);
}
