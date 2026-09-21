#ifndef AIKERNEL_MEMORY_H
#define AIKERNEL_MEMORY_H

#include <kernel/types.h>

void  early_heap_init(void);
void *kalloc_try(usize size, usize alignment);
void *kalloc(usize size, usize alignment);
usize early_heap_used(void);
usize early_heap_capacity(void);
usize early_heap_remaining(void);

#endif
