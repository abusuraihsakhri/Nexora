#ifndef NEXORA_X86_64_H
#define NEXORA_X86_64_H

#include <stdint.h>
#include <stddef.h>

#define NEXORA_GDT_KERNEL_CODE 0x08u
#define NEXORA_GDT_KERNEL_DATA 0x10u
#define NEXORA_GDT_USER_DATA   0x18u
#define NEXORA_GDT_USER_CODE   0x20u
#define NEXORA_GDT_TSS         0x28u

void nexora_x86_gdt_init(uintptr_t rsp0);
void nexora_x86_set_kernel_stack(uintptr_t rsp0);
void nexora_x86_syscall_init(uintptr_t kernel_stack_top);
void nexora_x86_enter_user(uintptr_t rip, uintptr_t rsp) __attribute__((noreturn));

struct nexora_percpu {
    uint64_t kernel_rsp;        /* offset 0 */
    uint64_t user_rsp;          /* offset 8 */
    uint32_t cpu_id;            /* offset 16 */
    uint32_t nest_depth;        /* offset 20 */
    uint64_t saved_user_rsp[4]; /* offset 24 */
};

extern struct nexora_percpu nexora_bsp_percpu;
extern uintptr_t nexora_syscall_kernel_rsp;
extern uintptr_t nexora_syscall_user_rsp;

static inline void nexora_percpu_init(struct nexora_percpu *cpu, uint32_t cpu_id, uintptr_t kernel_rsp) {
    if (!cpu) return;
    cpu->kernel_rsp = kernel_rsp;
    cpu->user_rsp = 0;
    cpu->cpu_id = cpu_id;
    cpu->nest_depth = 0;
    for (int i = 0; i < 4; ++i) cpu->saved_user_rsp[i] = 0;
}

static inline int nexora_percpu_push_user_rsp(struct nexora_percpu *cpu, uint64_t rsp) {
    if (!cpu || cpu->nest_depth >= 4) return -1;
    cpu->saved_user_rsp[cpu->nest_depth++] = rsp;
    cpu->user_rsp = rsp;
    return 0;
}

static inline uint64_t nexora_percpu_pop_user_rsp(struct nexora_percpu *cpu) {
    if (!cpu || cpu->nest_depth == 0) return 0;
    uint64_t val = cpu->saved_user_rsp[--cpu->nest_depth];
    cpu->user_rsp = (cpu->nest_depth > 0) ? cpu->saved_user_rsp[cpu->nest_depth - 1] : 0;
    return val;
}

#endif
