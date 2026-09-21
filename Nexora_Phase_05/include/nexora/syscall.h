#ifndef NEXORA_SYSCALL_H
#define NEXORA_SYSCALL_H

#include <stdint.h>
#include <nexora/abi.h>
#include <nexora/process.h>

struct nexora_syscall_frame {
    uint64_t r9;
    uint64_t r8;
    uint64_t r10;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rax;
    uint64_t user_rflags;
    uint64_t user_rip;
};

void nexora_syscall_set_current_process(struct nexora_process *process);
struct nexora_process *nexora_syscall_current_process(void);
void nexora_syscall_process_cleanup(struct nexora_process *process);

nexora_status_t nexora_syscall_dispatch(uint64_t number,
                                        uint64_t a0,
                                        uint64_t a1,
                                        uint64_t a2,
                                        uint64_t a3,
                                        uint64_t a4,
                                        uint64_t a5);
void nexora_syscall_dispatch_frame(struct nexora_syscall_frame *frame);
void nexora_syscall_bad_rip_fault(struct nexora_syscall_frame *frame);

static inline bool nexora_is_canonical_user_rip(uint64_t rip) {
    return (rip >> 47) == 0;
}

#endif
