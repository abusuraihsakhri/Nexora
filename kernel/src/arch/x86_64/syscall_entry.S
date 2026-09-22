.section .text
.code64
.global nexora_syscall_entry
.extern nexora_syscall_dispatch_frame
.extern nexora_syscall_bad_rip_fault

/*
 * Nexora x86-64 syscall ABI:
 *   rax = syscall number
 *   rdi,rsi,rdx,r10,r8,r9 = arguments 0..5
 *   rax = signed result
 *
 * swapgs selects the C-defined per-CPU structure through IA32_KERNEL_GS_BASE:
 *   %gs:0 = kernel_rsp
 *   %gs:8 = user_rsp
 *
 * The per-CPU object has exactly one storage definition, in syscall_msr.c.
 * Keeping storage out of this assembly file avoids duplicate linker symbols.
 */
nexora_syscall_entry:
    swapgs
    movq %rsp, %gs:8
    movq %gs:0, %rsp

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
     * Security check: Return RIP (at 64(%rsp)) must be a canonical lower-half
     * user address. SYSRET with a non-canonical RCX may fault in Ring 0.
     */
    movq 64(%rsp), %rcx
    movq %rcx, %rax
    shrq $47, %rax
    jnz .Lsyscall_sysret_non_canonical

    /*
     * Preserve syscall argument registers across the transition. The userspace
     * wrapper declares only RCX/R11 as architectural clobbers.
     */
    movq 0(%rsp), %r9
    movq 8(%rsp), %r8
    movq 16(%rsp), %r10
    movq 24(%rsp), %rdx
    movq 32(%rsp), %rsi
    movq 40(%rsp), %rdi
    movq 48(%rsp), %rax
    movq 56(%rsp), %r11
    addq $72, %rsp

    movq %gs:8, %rsp
    swapgs
    sysretq

.Lsyscall_sysret_non_canonical:
    /*
     * Do not attempt SYSRET. Mark the current process dead and remain in the
     * kernel. A scheduler/process-exit path can replace this terminal loop once
     * Nexora grows runnable user processes.
     */
    movq %rsp, %rdi
    subq $8, %rsp
    call nexora_syscall_bad_rip_fault
    addq $8, %rsp
1:
    cli
    hlt
    jmp 1b

.section .note.GNU-stack,"",@progbits
