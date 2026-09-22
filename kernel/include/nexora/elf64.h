#ifndef NEXORA_ELF64_H
#define NEXORA_ELF64_H

#include <stddef.h>
#include <stdint.h>
#include <nexora/abi.h>
#include <nexora/process.h>

enum nexora_user_segment_flags {
    NEXORA_SEG_READ = 1u << 0,
    NEXORA_SEG_WRITE = 1u << 1,
    NEXORA_SEG_EXEC = 1u << 2,
};

struct nexora_user_vm_ops {
    nexora_status_t (*map_segment)(struct nexora_process *process,
                                   uintptr_t virtual_address,
                                   size_t memory_size,
                                   const void *file_data,
                                   size_t file_size,
                                   uint32_t flags,
                                   void *context);
};

nexora_status_t nexora_elf64_load(const void *image,
                                  size_t image_size,
                                  struct nexora_process *process,
                                  const struct nexora_user_vm_ops *vm_ops,
                                  void *vm_context,
                                  uintptr_t *entry_out);

#endif
