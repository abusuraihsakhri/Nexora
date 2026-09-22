.section .text
.code64

.global isr3_entry
.global isr8_entry
.global isr13_entry
.global isr14_entry
.global isr_default_entry
.extern isr_common_handler

/* Vector 3: #BP Breakpoint (No CPU error code) */
isr3_entry:
    pushq $0
    pushq $3
    jmp isr_common_stub

/* Vector 8: #DF Double Fault (CPU pushes error code) */
isr8_entry:
    pushq $8
    jmp isr_common_stub

/* Vector 13: #GP General Protection Fault (CPU pushes error code) */
isr13_entry:
    pushq $13
    jmp isr_common_stub

/* Vector 14: #PF Page Fault (CPU pushes error code) */
isr14_entry:
    pushq $14
    jmp isr_common_stub

/* Default ISR for all unhandled vectors */
isr_default_entry:
    pushq $0
    pushq $255
    jmp isr_common_stub

isr_common_stub:
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11

    movq %rsp, %rdi
    call isr_common_handler

    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rax

    addq $16, %rsp
    iretq
