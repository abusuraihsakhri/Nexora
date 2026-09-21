#include <kernel/memory.h>
#include <kernel/panic.h>

#define EARLY_HEAP_SIZE (1024 * 1024)

static u8 early_heap[EARLY_HEAP_SIZE] __attribute__((aligned(4096)));
static usize offset = 0;

void early_heap_init(void) {
    offset = 0;
}

void *kalloc(usize size, usize alignment) {
    if (alignment == 0) alignment = 1;

    usize aligned = (offset + alignment - 1) & ~(alignment - 1);

    if (aligned + size > EARLY_HEAP_SIZE) {
        panic("early heap exhausted");
    }

    void *ptr = &early_heap[aligned];
    offset = aligned + size;
    return ptr;
}

usize early_heap_used(void) {
    return offset;
}

usize early_heap_capacity(void) {
    return EARLY_HEAP_SIZE;
}
