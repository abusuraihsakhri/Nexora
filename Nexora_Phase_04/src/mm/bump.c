#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>

#define EARLY_HEAP_SIZE (1024 * 1024)

static u8 early_heap[EARLY_HEAP_SIZE] __attribute__((aligned(4096)));
static usize offset = 0;
static ai_spinlock heap_lock = AI_SPINLOCK_INITIALIZER;

void early_heap_init(void) {
    ai_spinlock_init(&heap_lock);
    ai_spin_lock(&heap_lock);
    offset = 0;
    ai_spin_unlock(&heap_lock);
}

void *kalloc(usize size, usize alignment) {
    if (alignment == 0) alignment = 1;

    ai_spin_lock(&heap_lock);
    usize aligned = (offset + alignment - 1) & ~(alignment - 1);

    if (aligned + size > EARLY_HEAP_SIZE) {
        ai_spin_unlock(&heap_lock);
        panic("early heap exhausted");
    }

    void *ptr = &early_heap[aligned];
    offset = aligned + size;
    ai_spin_unlock(&heap_lock);
    return ptr;
}

usize early_heap_used(void) {
    ai_spin_lock(&heap_lock);
    usize used = offset;
    ai_spin_unlock(&heap_lock);
    return used;
}

usize early_heap_capacity(void) {
    return EARLY_HEAP_SIZE;
}
