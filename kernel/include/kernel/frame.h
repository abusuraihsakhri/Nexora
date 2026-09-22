#ifndef AIKERNEL_FRAME_H
#define AIKERNEL_FRAME_H

#include <kernel/types.h>

#define PAGE_SIZE 4096u
#define MAX_FRAMES 32768u /* 128 MiB default physical frame capacity */

void frame_init(uintptr_t base_paddr, usize frame_count);
uintptr_t frame_alloc(void);
void frame_free(uintptr_t paddr);
usize frame_free_count(void);
usize frame_total_count(void);
bool frame_is_allocated(uintptr_t paddr);

#endif
