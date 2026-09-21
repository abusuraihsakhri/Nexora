#include <nexora/process.h>
#include <stddef.h>

static struct nexora_process *process_table[NEXORA_MAX_PROCESSES];

void nexora_process_system_init(void) {
    for (uint32_t i = 0; i < NEXORA_MAX_PROCESSES; ++i) process_table[i] = NULL;
}

nexora_status_t nexora_process_init(struct nexora_process *process,
                                    uint32_t pid,
                                    uintptr_t user_lo,
                                    uintptr_t user_hi) {
    if (!process || pid == 0 || user_lo >= user_hi) return NEXORA_ERR(NEXORA_EINVAL);
    for (uint32_t i = 0; i < NEXORA_MAX_PROCESSES; ++i) {
        if (process_table[i] && process_table[i]->alive && process_table[i]->pid == pid) {
            return NEXORA_ERR(NEXORA_EEXIST);
        }
    }
    process->pid = pid;
    process->parent_pid = 0;
    process->alive = 1;
    process->user_lo = user_lo;
    process->user_hi = user_hi;
    nexora_handle_table_init(&process->handles);
    for (uint32_t i = 0; i < NEXORA_MAX_PROCESSES; ++i) {
        if (!process_table[i] || !process_table[i]->alive) {
            process_table[i] = process;
            return NEXORA_OK;
        }
    }
    process->alive = 0;
    return NEXORA_ERR(NEXORA_ENOSPC);
}

void nexora_process_set_parent(struct nexora_process *process, uint32_t parent_pid) {
    if (process) {
        process->parent_pid = parent_pid;
    }
}

struct nexora_process *nexora_process_lookup(uint32_t pid) {
    for (uint32_t i = 0; i < NEXORA_MAX_PROCESSES; ++i) {
        if (process_table[i] && process_table[i]->alive && process_table[i]->pid == pid) return process_table[i];
    }
    return NULL;
}


nexora_status_t nexora_process_unregister(struct nexora_process *process) {
    if (!process) return NEXORA_ERR(NEXORA_EINVAL);
    for (uint32_t i = 0; i < NEXORA_MAX_PROCESSES; ++i) {
        if (process_table[i] == process) {
            process_table[i] = NULL;
            process->alive = 0;
            return NEXORA_OK;
        }
    }
    process->alive = 0;
    return NEXORA_ERR(NEXORA_ENOENT);
}
