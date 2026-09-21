#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <nexora/mm/vm/types.h>
typedef struct { uint64_t value; } x86_page_entry_t;
typedef struct { phys_addr_t root_phys; uint16_t pcid; } x86_vm_space_t;
bool x86_is_canonical(virt_addr_t va);
uint16_t x86_pml4_index(virt_addr_t va);
uint16_t x86_pdpt_index(virt_addr_t va);
uint16_t x86_pd_index(virt_addr_t va);
uint16_t x86_pt_index(virt_addr_t va);
vm_result_t x86_pt_translate(const x86_vm_space_t*,virt_addr_t,phys_addr_t*);
