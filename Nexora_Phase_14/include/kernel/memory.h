#ifndef AIKERNEL_MEMORY_H
#define AIKERNEL_MEMORY_H

#include <kernel/types.h>

typedef struct {
    usize used;
    usize capacity;
    usize allocations;
    usize requested_bytes;
    usize padding_bytes;
    usize high_watermark;
} early_heap_stats;

void  early_heap_init(void);
void *kalloc(usize size, usize alignment);
usize early_heap_used(void);
usize early_heap_capacity(void);
bool  early_heap_can_alloc(usize size, usize alignment);
void  early_heap_get_stats(early_heap_stats *out);

#endif
