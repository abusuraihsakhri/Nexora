#include <ai/backing.h>
#include <kernel/atomic.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>

static ai_tensor_backing *registry[AI_MAX_BACKINGS];
static u64 backing_count = 0;
static u64 next_backing_id = 1;
static ai_spinlock registry_lock = AI_SPINLOCK_INITIALIZER;

static u64 align_up_page(u64 value) {
    if (value == 0) return 0;
    return (value + AI_BACKING_PAGE_SIZE - 1ull) &
           ~(AI_BACKING_PAGE_SIZE - 1ull);
}

static void retire_backing(ai_tensor_backing *backing) {
    if (!backing) return;

    u32 expected = (u32)AI_BACKING_LIVE;
    if (!ai_atomic_compare_exchange_u32(
            (volatile u32 *)&backing->lifetime_state,
            &expected,
            (u32)AI_BACKING_RETIRED)) {
        return;
    }

    if (ai_atomic_load_u64(&backing->mapping_count) != 0) {
        panic("retiring tensor backing with live mappings");
    }

    ai_spin_lock(&registry_lock);
    for (u32 i = 0; i < AI_MAX_BACKINGS; ++i) {
        if (registry[i] == backing) {
            registry[i] = NULL;
            if (backing_count == 0) {
                ai_spin_unlock(&registry_lock);
                panic("tensor backing registry underflow");
            }
            backing_count--;
            break;
        }
    }
    ai_spin_unlock(&registry_lock);

    /* Physical recycling remains deferred until a real page allocator exists. */
}

void ai_backing_system_init(void) {
    ai_spinlock_init(&registry_lock);
    ai_spin_lock(&registry_lock);
    for (u32 i = 0; i < AI_MAX_BACKINGS; ++i) registry[i] = NULL;
    backing_count = 0;
    next_backing_id = 1;
    ai_spin_unlock(&registry_lock);
}

ai_tensor_backing *ai_backing_create_ram(u64 size_bytes, u32 flags) {
    if (size_bytes == 0) return NULL;

    u64 allocation_bytes = align_up_page(size_bytes);
    if (allocation_bytes < size_bytes) return NULL;

    ai_tensor_backing *backing =
        (ai_tensor_backing *)kalloc(sizeof(ai_tensor_backing), 16);
    void *storage = kalloc((usize)allocation_bytes, (usize)AI_BACKING_PAGE_SIZE);

    backing->kind = AI_BACKING_RAM;
    backing->size_bytes = size_bytes;
    backing->allocation_bytes = allocation_bytes;
    backing->page_count = allocation_bytes / AI_BACKING_PAGE_SIZE;
    backing->kernel_base = storage;
    backing->physical_base = (u64)(usize)storage;
    ai_atomic_store_u64(&backing->refcount, 1);
    ai_atomic_store_u64(&backing->mapping_count, 0);
    ai_atomic_store_u32(
        (volatile u32 *)&backing->lifetime_state,
        (u32)AI_BACKING_LIVE);
    backing->flags = flags;

    if ((flags & AI_BACKING_ZEROED) != 0) {
        u8 *bytes = (u8 *)storage;
        for (u64 i = 0; i < allocation_bytes; ++i) bytes[i] = 0;
    }

    ai_spin_lock(&registry_lock);
    if (backing_count >= AI_MAX_BACKINGS) {
        ai_spin_unlock(&registry_lock);
        return NULL;
    }

    backing->id = next_backing_id++;
    for (u32 i = 0; i < AI_MAX_BACKINGS; ++i) {
        if (registry[i] == NULL) {
            registry[i] = backing;
            backing_count++;
            ai_spin_unlock(&registry_lock);
            return backing;
        }
    }
    ai_spin_unlock(&registry_lock);
    panic("tensor backing registry invariant violated");
    return NULL;
}

bool ai_backing_get(ai_tensor_backing *backing) {
    if (!backing ||
        ai_atomic_load_u32((volatile u32 *)&backing->lifetime_state) !=
            (u32)AI_BACKING_LIVE) {
        return false;
    }

    u64 current = ai_atomic_load_u64(&backing->refcount);
    for (;;) {
        if (current == 0 || current == ~0ull) return false;
        u64 expected = current;
        if (ai_atomic_compare_exchange_u64(
                &backing->refcount, &expected, current + 1ull)) {
            return true;
        }
        current = expected;
    }
}

bool ai_backing_put(ai_tensor_backing *backing) {
    if (!backing ||
        ai_atomic_load_u32((volatile u32 *)&backing->lifetime_state) !=
            (u32)AI_BACKING_LIVE) {
        return false;
    }

    u64 current = ai_atomic_load_u64(&backing->refcount);
    for (;;) {
        if (current == 0) return false;
        u64 expected = current;
        if (ai_atomic_compare_exchange_u64(
                &backing->refcount, &expected, current - 1ull)) {
            if (current == 1ull) retire_backing(backing);
            return true;
        }
        current = expected;
    }
}

bool ai_backing_is_live(const ai_tensor_backing *backing) {
    return backing &&
        ai_atomic_load_u32((const volatile u32 *)&backing->lifetime_state) ==
            (u32)AI_BACKING_LIVE &&
        ai_atomic_load_u64(&backing->refcount) != 0;
}

u64 ai_backing_refcount(const ai_tensor_backing *backing) {
    return backing ? ai_atomic_load_u64(&backing->refcount) : 0;
}

u64 ai_backing_mapping_count(const ai_tensor_backing *backing) {
    return backing ? ai_atomic_load_u64(&backing->mapping_count) : 0;
}

bool ai_backing_mapping_acquire(ai_tensor_backing *backing) {
    if (!ai_backing_get(backing)) return false;

    u64 current = ai_atomic_load_u64(&backing->mapping_count);
    for (;;) {
        if (current == ~0ull) {
            (void)ai_backing_put(backing);
            return false;
        }
        u64 expected = current;
        if (ai_atomic_compare_exchange_u64(
                &backing->mapping_count, &expected, current + 1ull)) {
            return true;
        }
        current = expected;
    }
}

bool ai_backing_mapping_release(ai_tensor_backing *backing) {
    if (!backing) return false;

    u64 current = ai_atomic_load_u64(&backing->mapping_count);
    for (;;) {
        if (current == 0) return false;
        u64 expected = current;
        if (ai_atomic_compare_exchange_u64(
                &backing->mapping_count, &expected, current - 1ull)) {
            break;
        }
        current = expected;
    }

    if (!ai_backing_put(backing)) {
        panic("tensor backing mapping release lost reference");
    }
    return true;
}

u64 ai_backing_count(void) {
    ai_spin_lock(&registry_lock);
    u64 result = backing_count;
    ai_spin_unlock(&registry_lock);
    return result;
}

u64 ai_backing_physical_at(const ai_tensor_backing *backing, u64 offset) {
    if (!ai_backing_is_live(backing) || offset >= backing->allocation_bytes) {
        return 0;
    }
    return backing->physical_base + offset;
}

void *ai_backing_kernel_at(const ai_tensor_backing *backing, u64 offset) {
    if (!ai_backing_is_live(backing) || !backing->kernel_base ||
        offset >= backing->allocation_bytes) {
        return NULL;
    }
    return (void *)((u8 *)backing->kernel_base + offset);
}

const char *ai_backing_kind_name(ai_backing_kind kind) {
    switch (kind) {
        case AI_BACKING_RAM: return "RAM";
        case AI_BACKING_DEVICE: return "DEVICE";
        case AI_BACKING_PINNED: return "PINNED";
        case AI_BACKING_REMOTE: return "REMOTE";
        default: return "UNKNOWN";
    }
}

const char *ai_backing_lifetime_state_name(ai_backing_lifetime_state state) {
    switch (state) {
        case AI_BACKING_LIVE: return "LIVE";
        case AI_BACKING_RETIRED: return "RETIRED";
        default: return "UNKNOWN";
    }
}
