#pragma once
#include "types.h"
#include "space.h"
typedef enum { VM_ACCESS_READ, VM_ACCESS_WRITE, VM_ACCESS_EXECUTE } vm_access_t;
typedef struct {
    virt_addr_t address;
    uint64_t error_code;
    bool present, write, user, reserved_bit, instruction_fetch;
    uintptr_t instruction_pointer;
} vm_fault_info_t;
typedef enum {
    VM_FAULT_RESOLVED, VM_FAULT_INVALID_ADDRESS, VM_FAULT_PERMISSION_DENIED,
    VM_FAULT_GUARD, VM_FAULT_OUT_OF_MEMORY, VM_FAULT_CORRUPT_PAGETABLE,
    VM_FAULT_UNSUPPORTED, VM_FAULT_KERNEL_FATAL
} vm_fault_result_t;
vm_fault_result_t vm_handle_fault(vm_space_t*,const vm_fault_info_t*);
