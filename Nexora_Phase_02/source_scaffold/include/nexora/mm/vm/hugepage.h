#pragma once
#include "types.h"
#include "space.h"
vm_result_t vm_try_promote_2m(vm_space_t *space, virt_addr_t base);
vm_result_t vm_split_2m(vm_space_t *space, virt_addr_t base);
