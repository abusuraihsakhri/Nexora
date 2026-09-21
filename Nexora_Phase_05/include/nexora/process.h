#ifndef NEXORA_PROCESS_H
#define NEXORA_PROCESS_H

#include <stdint.h>
#include <stdbool.h>
#include <nexora/handle.h>

#define NEXORA_MAX_PROCESSES 32u

struct nexora_process {
    uint32_t pid;
    uint8_t alive;
    uint8_t reserved[3];
    uintptr_t user_lo;
    uintptr_t user_hi;
    struct nexora_handle_table handles;
};

void nexora_process_system_init(void);
nexora_status_t nexora_process_init(struct nexora_process *process,
                                    uint32_t pid,
                                    uintptr_t user_lo,
                                    uintptr_t user_hi);
struct nexora_process *nexora_process_lookup(uint32_t pid);
nexora_status_t nexora_process_unregister(struct nexora_process *process);

#endif
