#include <kernel/memory.h>
#include <kernel/panic.h>

#define EARLY_HEAP_SIZE (1024 * 1024)

static u8 early_heap[EARLY_HEAP_SIZE] __attribute__((aligned(4096)));
static usize offset = 0;

static bool is_power_of_two(usize value) {
    return value != 0 && (value & (value - 1u)) == 0;
}

void early_heap_init(void) {
    offset = 0;
}

void *kalloc_try(usize size, usize alignment) {
    if (alignment == 0) {
        alignment = 1;
    }

    if (!is_power_of_two(alignment)) {
        return NULL;
    }

    const usize max = ~(usize)0;
    if (offset > max - (alignment - 1u)) {
        return NULL;
    }

    usize aligned = (offset + alignment - 1u) & ~(alignment - 1u);
    if (size > EARLY_HEAP_SIZE || aligned > EARLY_HEAP_SIZE - size) {
        return NULL;
    }

    void *ptr = &early_heap[aligned];
    offset = aligned + size;
    return ptr;
}

void *kalloc(usize size, usize alignment) {
    void *ptr = kalloc_try(size, alignment);
    if (ptr == NULL) {
        panic("early heap exhausted or invalid allocation");
    }
    return ptr;
}

usize early_heap_used(void) {
    return offset;
}

usize early_heap_capacity(void) {
    return EARLY_HEAP_SIZE;
}

usize early_heap_remaining(void) {
    return EARLY_HEAP_SIZE - offset;
}
