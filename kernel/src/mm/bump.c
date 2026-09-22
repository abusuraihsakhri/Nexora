#include <kernel/memory.h>
#include <kernel/panic.h>

#define EARLY_HEAP_SIZE (1024 * 1024)

static u8 early_heap[EARLY_HEAP_SIZE] __attribute__((aligned(4096)));
static usize offset = 0;
static usize allocation_count = 0;
static usize requested_bytes = 0;
static usize padding_bytes = 0;
static usize high_watermark = 0;

static bool valid_alignment(usize alignment) {
    return alignment != 0 && (alignment & (alignment - 1)) == 0;
}

static usize aligned_offset(usize current, usize alignment) {
    if (alignment == 0) alignment = 1;
    return (current + alignment - 1) & ~(alignment - 1);
}

void early_heap_init(void) {
    offset = 0;
    allocation_count = 0;
    requested_bytes = 0;
    padding_bytes = 0;
    high_watermark = 0;
}

bool early_heap_can_alloc(usize size, usize alignment) {
    if (alignment == 0) alignment = 1;
    if (!valid_alignment(alignment)) return false;

    usize aligned = aligned_offset(offset, alignment);
    if (aligned < offset) return false;
    if (size > EARLY_HEAP_SIZE - aligned) return false;
    return true;
}

void *kalloc(usize size, usize alignment) {
    if (alignment == 0) alignment = 1;
    if (!valid_alignment(alignment)) {
        panic("kalloc alignment must be a power of two");
    }
    if (!early_heap_can_alloc(size, alignment)) {
        panic("early heap exhausted");
    }

    usize aligned = aligned_offset(offset, alignment);
    usize padding = aligned - offset;
    void *ptr = &early_heap[aligned];

    offset = aligned + size;
    allocation_count++;
    requested_bytes += size;
    padding_bytes += padding;
    if (offset > high_watermark) high_watermark = offset;

    return ptr;
}

usize early_heap_used(void) {
    return offset;
}

usize early_heap_capacity(void) {
    return EARLY_HEAP_SIZE;
}

void early_heap_get_stats(early_heap_stats *out) {
    if (!out) return;
    out->used = offset;
    out->capacity = EARLY_HEAP_SIZE;
    out->allocations = allocation_count;
    out->requested_bytes = requested_bytes;
    out->padding_bytes = padding_bytes;
    out->high_watermark = high_watermark;
}
