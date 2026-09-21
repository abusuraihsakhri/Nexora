.section .text
.code64
.global nexora_syscall_entry
.global nexora_syscall_kernel_rsp
.global nexora_syscall_user_rsp
.extern nexora_syscall_dispatch_frame

/*
 * Nexora x86-64 syscall ABI:
 *   rax = syscall number
 *   rdi,rsi,rdx,r10,r8,r9 = arguments 0..5
 *   rax = signed result
 *
 * This bootstrap path uses one kernel stack pointer. It must be replaced by
 * per-CPU storage before concurrent SMP userspace is enabled.
 */
nexora_syscall_entry:
    swapgs
    movq %rsp, nexora_syscall_user_rsp(%rip)
    movq nexora_syscall_kernel_rsp(%rip), %rsp

    /* Build struct nexora_syscall_frame in reverse order. */
    pushq %rcx
    pushq %r11
    pushq %rax
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %r10
    pushq %r8
    pushq %r9

    /* Keep the SysV C-call stack 16-byte aligned before CALL. */
    movq %rsp, %rdi
    subq $8, %rsp
    call nexora_syscall_dispatch_frame
    addq $8, %rsp

    /*
     * Preserve all syscall argument registers across the transition.  The
     * userspace wrapper only declares RCX/R11 as architectural clobbers, so
     * returning the argument registers prevents a compiler/kernel ABI mismatch.
     */
    movq 0(%rsp), %r9
    movq 8(%rsp), %r8
    movq 16(%rsp), %r10
    movq 24(%rsp), %rdx
    movq 32(%rsp), %rsi
    movq 40(%rsp), %rdi
    movq 48(%rsp), %rax
    movq 56(%rsp), %r11
    movq 64(%rsp), %rcx
    addq $72, %rsp

    movq nexora_syscall_user_rsp(%rip), %rsp
    swapgs
    sysretq

.section .bss
.align 8
nexora_syscall_kernel_rsp:
    .quad 0
nexora_syscall_user_rsp:
    .quad 0
