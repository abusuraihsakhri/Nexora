#include <nexora/elf64.h>
#include <stdbool.h>

#define EI_NIDENT 16u
#define ELFCLASS64 2u
#define ELFDATA2LSB 1u
#define ET_EXEC 2u
#define EM_X86_64 62u
#define EV_CURRENT 1u
#define PT_LOAD 1u
#define PF_X 1u
#define PF_W 2u
#define PF_R 4u

struct __attribute__((packed)) elf64_ehdr {
    unsigned char ident[EI_NIDENT];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct __attribute__((packed)) elf64_phdr {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
};

static bool add_overflow_u64(uint64_t a, uint64_t b, uint64_t *out) {
    *out = a + b;
    return *out < a;
}

static bool range_in_image(uint64_t offset, uint64_t length, size_t image_size) {
    uint64_t end;
    if (add_overflow_u64(offset, length, &end)) return false;
    return end <= (uint64_t)image_size;
}

static bool user_range_valid(const struct nexora_process *process, uint64_t address, uint64_t length) {
    uint64_t end;
    if (!process || length == 0) return false;
    if (add_overflow_u64(address, length, &end)) return false;
    return address >= process->user_lo && end <= process->user_hi && end > address;
}

nexora_status_t nexora_elf64_load(const void *image,
                                  size_t image_size,
                                  struct nexora_process *process,
                                  const struct nexora_user_vm_ops *vm_ops,
                                  void *vm_context,
                                  uintptr_t *entry_out) {
    if (!image || !process || !vm_ops || !vm_ops->map_segment || !entry_out) {
        return NEXORA_ERR(NEXORA_EINVAL);
    }
    if (image_size < sizeof(struct elf64_ehdr)) return NEXORA_ERR(NEXORA_EINVAL);

    const unsigned char *bytes = (const unsigned char *)image;
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)image;
    if (eh->ident[0] != 0x7f || eh->ident[1] != 'E' || eh->ident[2] != 'L' || eh->ident[3] != 'F' ||
        eh->ident[4] != ELFCLASS64 || eh->ident[5] != ELFDATA2LSB || eh->ident[6] != EV_CURRENT ||
        eh->type != ET_EXEC || eh->machine != EM_X86_64 || eh->version != EV_CURRENT ||
        eh->ehsize < sizeof(*eh) || eh->phentsize != sizeof(struct elf64_phdr)) {
        return NEXORA_ERR(NEXORA_EINVAL);
    }

    uint64_t ph_bytes = (uint64_t)eh->phentsize * eh->phnum;
    if (!range_in_image(eh->phoff, ph_bytes, image_size)) return NEXORA_ERR(NEXORA_EINVAL);
    if (eh->entry < process->user_lo || eh->entry >= process->user_hi) return NEXORA_ERR(NEXORA_EACCES);

    /* Validation pass: reject malformed images before making any VM changes. */
    bool loaded_any = false;
    bool entry_in_executable_segment = false;
    for (uint16_t i = 0; i < eh->phnum; ++i) {
        const struct elf64_phdr *ph = (const struct elf64_phdr *)(bytes + eh->phoff + (uint64_t)i * eh->phentsize);
        if (ph->type != PT_LOAD) continue;
        if (ph->memsz == 0 || ph->filesz > ph->memsz) return NEXORA_ERR(NEXORA_EINVAL);
        if (!range_in_image(ph->offset, ph->filesz, image_size)) return NEXORA_ERR(NEXORA_EINVAL);
        if (!user_range_valid(process, ph->vaddr, ph->memsz)) return NEXORA_ERR(NEXORA_EACCES);
        if ((ph->flags & PF_W) && (ph->flags & PF_X)) return NEXORA_ERR(NEXORA_EACCES);
        if (ph->align > 1u) {
            if ((ph->align & (ph->align - 1u)) != 0) return NEXORA_ERR(NEXORA_EINVAL);
            if ((ph->vaddr & (ph->align - 1u)) != (ph->offset & (ph->align - 1u))) {
                return NEXORA_ERR(NEXORA_EINVAL);
            }
        }

        uint64_t segment_end = ph->vaddr + ph->memsz;
        if ((ph->flags & PF_X) && eh->entry >= ph->vaddr && eh->entry < segment_end) {
            entry_in_executable_segment = true;
        }
        loaded_any = true;
    }

    if (!loaded_any) return NEXORA_ERR(NEXORA_EINVAL);
    if (!entry_in_executable_segment) return NEXORA_ERR(NEXORA_EACCES);

    /* Pairwise overlap check: reject images with intersecting PT_LOAD segments */
    for (uint16_t i = 0; i < eh->phnum; ++i) {
        const struct elf64_phdr *phi = (const struct elf64_phdr *)(bytes + eh->phoff + (uint64_t)i * eh->phentsize);
        if (phi->type != PT_LOAD) continue;
        uint64_t start_i = phi->vaddr;
        uint64_t end_i = phi->vaddr + phi->memsz;

        for (uint16_t j = i + 1; j < eh->phnum; ++j) {
            const struct elf64_phdr *phj = (const struct elf64_phdr *)(bytes + eh->phoff + (uint64_t)j * eh->phentsize);
            if (phj->type != PT_LOAD) continue;
            uint64_t start_j = phj->vaddr;
            uint64_t end_j = phj->vaddr + phj->memsz;

            if (start_i < end_j && start_j < end_i) {
                return NEXORA_ERR(NEXORA_EINVAL);
            }
        }
    }

    /* Mapping pass: all purely structural validation has succeeded. */
    for (uint16_t i = 0; i < eh->phnum; ++i) {
        const struct elf64_phdr *ph = (const struct elf64_phdr *)(bytes + eh->phoff + (uint64_t)i * eh->phentsize);
        if (ph->type != PT_LOAD) continue;

        uint32_t flags = 0;
        if (ph->flags & PF_R) flags |= NEXORA_SEG_READ;
        if (ph->flags & PF_W) flags |= NEXORA_SEG_WRITE;
        if (ph->flags & PF_X) flags |= NEXORA_SEG_EXEC;

        nexora_status_t status = vm_ops->map_segment(process, (uintptr_t)ph->vaddr,
                                                      (size_t)ph->memsz,
                                                      bytes + ph->offset,
                                                      (size_t)ph->filesz,
                                                      flags, vm_context);
        if (status != NEXORA_OK) return status;
    }

    *entry_out = (uintptr_t)eh->entry;
    return NEXORA_OK;
}
