#pragma once
#include "types.h"
#include "space.h"
typedef struct {
    bool mapped;
    phys_addr_t physical;
    page_size_t page_size;
    vm_permissions_t permissions;
    vm_flags_t flags;
} vm_mapping_info_t;
vm_result_t vm_map_page(vm_space_t*,virt_page_t,phys_frame_t,vm_permissions_t,vm_flags_t);
vm_result_t vm_unmap_page(vm_space_t*,virt_page_t,vm_mapping_info_t*);
vm_result_t vm_protect_page(vm_space_t*,virt_page_t,vm_permissions_t);
vm_result_t vm_query_mapping(vm_space_t*,virt_addr_t,vm_mapping_info_t*);
vm_result_t vm_translate(vm_space_t*,virt_addr_t,phys_addr_t*);
