#pragma once
#include "types.h"
#include "space.h"
typedef struct vm_object vm_object_t;
typedef struct vm_region vm_region_t;
typedef uint64_t vm_region_flags_t;
enum {
    VM_REGION_ANONYMOUS=1ull<<0, VM_REGION_SHARED=1ull<<1, VM_REGION_PRIVATE=1ull<<2,
    VM_REGION_GROW_DOWN=1ull<<3, VM_REGION_GROW_UP=1ull<<4, VM_REGION_GUARD=1ull<<5,
    VM_REGION_COW=1ull<<6, VM_REGION_LAZY=1ull<<7, VM_REGION_DEVICE=1ull<<8,
    VM_REGION_PINNED=1ull<<9, VM_REGION_HUGE_PREFER=1ull<<10,
    VM_REGION_HUGE_REQUIRE=1ull<<11
};
vm_result_t vm_region_create(vm_space_t*,virt_addr_t,size_t,vm_permissions_t,vm_region_flags_t,vm_object_t*,uint64_t,vm_region_t**);
vm_result_t vm_region_remove(vm_space_t*,virt_addr_t,size_t);
vm_result_t vm_region_protect(vm_space_t*,virt_addr_t,size_t,vm_permissions_t);
vm_region_t *vm_region_find(vm_space_t*,virt_addr_t);
bool vm_region_overlaps(vm_space_t*,virt_addr_t,virt_addr_t);
void vm_regions_dump(const vm_space_t*);
