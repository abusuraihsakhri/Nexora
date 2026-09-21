#pragma once
#include "types.h"
typedef struct vm_space vm_space_t;
vm_result_t vm_space_create(vm_space_t **out);
void vm_space_get(vm_space_t *space);
void vm_space_put(vm_space_t *space);
vm_result_t vm_space_activate(vm_space_t *space);
vm_space_t *vm_space_current(void);
vm_result_t vm_space_clone_cow(vm_space_t *parent, vm_space_t **out_child);
bool vm_space_is_user_address(const vm_space_t *space, virt_addr_t address);
bool vm_space_is_kernel_address(virt_addr_t address);
bool vm_space_validate(const vm_space_t *space);
void vm_space_dump(const vm_space_t *space);
