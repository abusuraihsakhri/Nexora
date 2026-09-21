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

extern uintptr_t nexora_syscall_kernel_rsp;
extern uintptr_t nexora_syscall_user_rsp;

#endif
