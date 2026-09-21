.section .text
.code64
.global nexora_x86_enter_user

/* void nexora_x86_enter_user(uintptr_t rip, uintptr_t rsp) */
nexora_x86_enter_user:
    cli
    movw $0x1b, %ax
    movw %ax, %ds
    movw %ax, %es

    pushq $0x1b
    pushq %rsi
    pushfq
    popq %rax
    orq $0x200, %rax
    pushq %rax
    pushq $0x23
    pushq %rdi
    iretq

1:
    hlt
    jmp 1b
