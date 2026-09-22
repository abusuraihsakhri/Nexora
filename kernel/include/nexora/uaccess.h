#ifndef NEXORA_UACCESS_H
#define NEXORA_UACCESS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <nexora/abi.h>
#include <nexora/process.h>

typedef bool (*nexora_user_range_validator)(const struct nexora_process *process,
                                            uintptr_t address,
                                            size_t length,
                                            bool write);

void nexora_uaccess_set_validator(nexora_user_range_validator validator);
bool nexora_uaccess_range_ok(const struct nexora_process *process,
                             const void *user_ptr,
                             size_t length,
                             bool write);
nexora_status_t nexora_copy_from_user(const struct nexora_process *process,
                                      void *kernel_dst,
                                      const void *user_src,
                                      size_t length);
nexora_status_t nexora_copy_to_user(const struct nexora_process *process,
                                    void *user_dst,
                                    const void *kernel_src,
                                    size_t length);

struct nexora_fixup_entry {
    uintptr_t fault_addr;
    uintptr_t fixup_addr;
};

void nexora_exception_fixup_register(uintptr_t fault_addr, uintptr_t fixup_addr);
uintptr_t nexora_exception_fixup_lookup(uintptr_t fault_ip);

typedef bool (*nexora_page_fault_probe_t)(uintptr_t addr, size_t len, bool write);
void nexora_uaccess_set_fault_probe(nexora_page_fault_probe_t probe);

#endif
