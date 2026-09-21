#include "nexora.h"
#include <stdint.h>

static inline nexora_status_t nx_syscall6(uint64_t nr, uint64_t a0, uint64_t a1,
                                          uint64_t a2, uint64_t a3, uint64_t a4,
                                          uint64_t a5) {
    register uint64_t r10 __asm__("r10") = a3;
    register uint64_t r8 __asm__("r8") = a4;
    register uint64_t r9 __asm__("r9") = a5;
    register uint64_t rax __asm__("rax") = nr;
    register uint64_t rdi __asm__("rdi") = a0;
    register uint64_t rsi __asm__("rsi") = a1;
    register uint64_t rdx __asm__("rdx") = a2;
    __asm__ volatile("syscall"
                     : "+a"(rax)
                     : "D"(rdi), "S"(rsi), "d"(rdx), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return (nexora_status_t)rax;
}

nexora_status_t nexora_abi_query(struct nexora_abi_info *info) {
    return nx_syscall6(NEXORA_SYS_ABI_QUERY, (uint64_t)(uintptr_t)info, 0, 0, 0, 0, 0);
}

nexora_status_t ai_tensor_create(const struct nexora_tensor_desc *desc, nexora_handle_t *handle) {
    return nx_syscall6(NEXORA_SYS_AI_TENSOR_CREATE, (uint64_t)(uintptr_t)desc,
                       (uint64_t)(uintptr_t)handle, 0, 0, 0, 0);
}

nexora_status_t ai_tensor_map(nexora_handle_t handle, const struct nexora_tensor_map *request, uintptr_t *address) {
    return nx_syscall6(NEXORA_SYS_AI_TENSOR_MAP, handle, (uint64_t)(uintptr_t)request,
                       (uint64_t)(uintptr_t)address, 0, 0, 0);
}

nexora_status_t ai_tensor_release(nexora_handle_t handle) {
    return nx_syscall6(NEXORA_SYS_AI_TENSOR_RELEASE, handle, 0, 0, 0, 0, 0);
}

nexora_status_t ai_work_submit(const struct nexora_work_desc *desc, nexora_handle_t *handle) {
    return nx_syscall6(NEXORA_SYS_AI_WORK_SUBMIT, (uint64_t)(uintptr_t)desc,
                       (uint64_t)(uintptr_t)handle, 0, 0, 0, 0);
}

nexora_status_t ai_work_wait(nexora_handle_t handle, uint64_t timeout_ns, struct nexora_work_result *result) {
    return nx_syscall6(NEXORA_SYS_AI_WORK_WAIT, handle, timeout_ns,
                       (uint64_t)(uintptr_t)result, 0, 0, 0);
}

nexora_status_t ai_cap_delegate(struct nexora_cap_delegate *request) {
    return nx_syscall6(NEXORA_SYS_AI_CAP_DELEGATE, (uint64_t)(uintptr_t)request, 0, 0, 0, 0, 0);
}

nexora_status_t ai_device_query(uint32_t ordinal, struct nexora_device_info *info) {
    return nx_syscall6(NEXORA_SYS_AI_DEVICE_QUERY, ordinal, (uint64_t)(uintptr_t)info, 0, 0, 0, 0);
}
