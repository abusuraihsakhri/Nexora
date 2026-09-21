#pragma once
#include <stddef.h>
#include <nexora/mm/vm/types.h>
void x86_tlb_invalidate_page(virt_addr_t address);
void x86_tlb_invalidate_range(virt_addr_t start,size_t length);
void x86_tlb_flush_current_context(void);
