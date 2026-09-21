#include <nexora/uaccess.h>

static nexora_user_range_validator range_validator;

void nexora_uaccess_set_validator(nexora_user_range_validator validator) {
    range_validator = validator;
}

static bool bounds_ok(const struct nexora_process *process, uintptr_t address, size_t length) {
    if (!process || !process->alive) return false;
    if (length == 0) return true;
    if (address < process->user_lo || address >= process->user_hi) return false;
    uintptr_t end = address + length;
    if (end < address || end > process->user_hi) return false;
    return true;
}

bool nexora_uaccess_range_ok(const struct nexora_process *process,
                             const void *user_ptr,
                             size_t length,
                             bool write) {
    uintptr_t address = (uintptr_t)user_ptr;
    if (!bounds_ok(process, address, length)) return false;
    if (length == 0) return true;
    if (!range_validator) return false;
    if (!range_validator(process, address, length, write)) return false;
    return true;
}

static void byte_copy(void *dst, const void *src, size_t length) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < length; ++i) d[i] = s[i];
}

nexora_status_t nexora_copy_from_user(const struct nexora_process *process,
                                      void *kernel_dst,
                                      const void *user_src,
                                      size_t length) {
    if (!kernel_dst || !nexora_uaccess_range_ok(process, user_src, length, false)) {
        return NEXORA_ERR(NEXORA_EFAULT);
    }
    byte_copy(kernel_dst, user_src, length);
    return NEXORA_OK;
}

nexora_status_t nexora_copy_to_user(const struct nexora_process *process,
                                    void *user_dst,
                                    const void *kernel_src,
                                    size_t length) {
    if (!kernel_src || !nexora_uaccess_range_ok(process, user_dst, length, true)) {
        return NEXORA_ERR(NEXORA_EFAULT);
    }
    byte_copy(user_dst, kernel_src, length);
    return NEXORA_OK;
}
