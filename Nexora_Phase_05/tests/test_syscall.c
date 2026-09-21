#include <nexora/abi.h>
#include <nexora/backend.h>
#include <nexora/handle.h>
#include <nexora/process.h>
#include <nexora/syscall.h>
#include <nexora/elf64.h>
#include <nexora/uaccess.h>
#include <nexora/x86_64.h>
#include "mock_backend.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct nexora_percpu nexora_bsp_percpu;

#define ASSERT_EQ(actual, expected) do { \
    long long _a = (long long)(actual); \
    long long _e = (long long)(expected); \
    if (_a != _e) { \
        fprintf(stderr, "ASSERT_EQ failed at %s:%d: got %lld expected %lld\n", __FILE__, __LINE__, _a, _e); \
        exit(1); \
    } \
} while (0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "ASSERT_TRUE failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static struct nexora_process p1, p2;
static uintptr_t denied_write_start;
static uintptr_t denied_write_end;

static bool permissive_validator(const struct nexora_process *process,
                                 uintptr_t address,
                                 size_t length,
                                 bool write) {
    (void)process; (void)address; (void)length; (void)write;
    return true;
}

static bool selective_validator(const struct nexora_process *process,
                                uintptr_t address,
                                size_t length,
                                bool write) {
    (void)process;
    if (!write || length == 0) return true;
    uintptr_t end = address + length;
    if (end < address) return false;
    return end <= denied_write_start || address >= denied_write_end;
}

static void reset_runtime(void) {
    nexora_process_system_init();
    ASSERT_EQ(nexora_process_init(&p1, 1, 1, UINTPTR_MAX), NEXORA_OK);
    ASSERT_EQ(nexora_process_init(&p2, 2, 1, UINTPTR_MAX), NEXORA_OK);
    nexora_process_set_parent(&p2, p1.pid);
    denied_write_start = 0;
    denied_write_end = 0;
    nexora_uaccess_set_validator(permissive_validator);
    mock_backend_reset();
    mock_backend_install();
    nexora_syscall_set_current_process(&p1);
}

static nexora_handle_t create_tensor(uint32_t flags) {
    struct nexora_tensor_desc desc = {
        .struct_size = sizeof(desc),
        .dtype = NEXORA_DTYPE_F16,
        .ndim = 2,
        .location = NEXORA_LOC_CPU_RAM,
        .flags = flags,
        .shape = {32, 32},
    };
    nexora_handle_t handle = 0;
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_TENSOR_CREATE,
        (uintptr_t)&desc, (uintptr_t)&handle, 0, 0, 0, 0), NEXORA_OK);
    ASSERT_TRUE(handle != 0);
    return handle;
}

static void test_abi_query(void) {
    reset_runtime();
    struct nexora_abi_info info = {0};
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_ABI_QUERY, (uintptr_t)&info, 0, 0, 0, 0, 0), NEXORA_OK);
    ASSERT_EQ(info.abi_version, NEXORA_ABI_VERSION);
    ASSERT_EQ(info.syscall_count, NEXORA_SYS_MAX);
    ASSERT_EQ(info.pointer_bits, 64);
}

static void test_abi_query_without_backend(void) {
    nexora_process_system_init();
    ASSERT_EQ(nexora_process_init(&p1, 1, 1, UINTPTR_MAX), NEXORA_OK);
    nexora_uaccess_set_validator(permissive_validator);
    nexora_backend_install(NULL);
    nexora_syscall_set_current_process(&p1);
    struct nexora_abi_info info = {0};
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_ABI_QUERY, (uintptr_t)&info, 0, 0, 0, 0, 0), NEXORA_OK);
    ASSERT_EQ(info.abi_version, NEXORA_ABI_VERSION);
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_DEVICE_QUERY, 0, (uintptr_t)&info, 0, 0, 0, 0),
              NEXORA_ERR(NEXORA_ENOSYS));
}

static void test_uaccess_requires_validator(void) {
    reset_runtime();
    struct nexora_abi_info info = {0};
    nexora_uaccess_set_validator(NULL);
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_ABI_QUERY, (uintptr_t)&info, 0, 0, 0, 0, 0),
              NEXORA_ERR(NEXORA_EFAULT));
    nexora_uaccess_set_validator(permissive_validator);
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_ABI_QUERY, (uintptr_t)&info, 0, 0, 0, 0, 0), NEXORA_OK);
}

static void test_tensor_lifecycle_and_stale_handle(void) {
    reset_runtime();
    nexora_handle_t handle = create_tensor(0);
    ASSERT_EQ(mock_live_tensors(), 1);

    struct nexora_tensor_map request = {
        .struct_size = sizeof(request),
        .flags = NEXORA_MAP_READ | NEXORA_MAP_WRITE,
    };
    uintptr_t address = 0;
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_TENSOR_MAP, handle,
        (uintptr_t)&request, (uintptr_t)&address, 0, 0, 0), NEXORA_OK);
    ASSERT_TRUE(address >= 0x40000000ull);
    nexora_handle_t forged = handle | (1ull << 63);
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_TENSOR_MAP, forged,
        (uintptr_t)&request, (uintptr_t)&address, 0, 0, 0), NEXORA_ERR(NEXORA_EINVAL));

    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_TENSOR_RELEASE, handle, 0, 0, 0, 0, 0), NEXORA_OK);
    ASSERT_EQ(mock_live_tensors(), 0);
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_TENSOR_MAP, handle,
        (uintptr_t)&request, (uintptr_t)&address, 0, 0, 0), NEXORA_ERR(NEXORA_ESTALE));
}

static void test_tensor_map_output_fault_has_no_backend_side_effect(void) {
    reset_runtime();
    nexora_handle_t handle = create_tensor(0);
    struct nexora_tensor_map request = {
        .struct_size = sizeof(request),
        .flags = NEXORA_MAP_READ,
    };
    uintptr_t address = 0;
    denied_write_start = (uintptr_t)&address;
    denied_write_end = denied_write_start + sizeof(address);
    nexora_uaccess_set_validator(selective_validator);
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_TENSOR_MAP, handle,
        (uintptr_t)&request, (uintptr_t)&address, 0, 0, 0), NEXORA_ERR(NEXORA_EFAULT));
    ASSERT_EQ(mock_tensor_map_calls(), 0);
}

static void test_readonly_rights_enforced(void) {
    reset_runtime();
    nexora_handle_t handle = create_tensor(NEXORA_TENSOR_READONLY);
    struct nexora_tensor_map request = {
        .struct_size = sizeof(request),
        .flags = NEXORA_MAP_READ | NEXORA_MAP_WRITE,
    };
    uintptr_t address = 0;
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_TENSOR_MAP, handle,
        (uintptr_t)&request, (uintptr_t)&address, 0, 0, 0), NEXORA_ERR(NEXORA_EACCES));
    request.flags = NEXORA_MAP_READ;
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_TENSOR_MAP, handle,
        (uintptr_t)&request, (uintptr_t)&address, 0, 0, 0), NEXORA_OK);
}

static void test_work_submit_wait(void) {
    reset_runtime();
    nexora_handle_t input = create_tensor(0);
    nexora_handle_t output = create_tensor(0);
    struct nexora_work_desc desc = {
        .struct_size = sizeof(desc),
        .op = NEXORA_OP_MATMUL,
        .priority = 5,
        .device_mask = NEXORA_DEVICE_GPU,
        .deadline_ns = 5000000,
        .batch_id = 77,
        .input_count = 1,
        .output_count = 1,
        .inputs = {input},
        .outputs = {output},
    };
    nexora_handle_t work = 0;
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_WORK_SUBMIT,
        (uintptr_t)&desc, (uintptr_t)&work, 0, 0, 0, 0), NEXORA_OK);
    ASSERT_TRUE(work != 0);
    ASSERT_EQ(mock_live_works(), 1);

    struct nexora_work_result result = {.struct_size = sizeof(result)};
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_WORK_WAIT, work, 1000,
        (uintptr_t)&result, 0, 0, 0), NEXORA_OK);
    ASSERT_EQ(result.state, NEXORA_WORK_DONE);
    ASSERT_EQ(result.device_id, 0);
    ASSERT_EQ(mock_live_works(), 0);
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_WORK_WAIT, work, 1000,
        (uintptr_t)&result, 0, 0, 0), NEXORA_ERR(NEXORA_ESTALE));
}

static void test_work_submit_output_fault_rolls_back(void) {
    reset_runtime();
    nexora_handle_t input = create_tensor(0);
    struct nexora_work_desc desc = {
        .struct_size = sizeof(desc), .op = NEXORA_OP_NOOP,
        .device_mask = NEXORA_DEVICE_CPU, .input_count = 1, .inputs = {input},
    };
    nexora_handle_t work = 0;
    denied_write_start = (uintptr_t)&work;
    denied_write_end = denied_write_start + sizeof(work);
    nexora_uaccess_set_validator(selective_validator);
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_WORK_SUBMIT,
        (uintptr_t)&desc, (uintptr_t)&work, 0, 0, 0, 0), NEXORA_ERR(NEXORA_EFAULT));
    ASSERT_EQ(mock_live_works(), 0);
}

static void test_wait_timeout(void) {
    reset_runtime();
    nexora_handle_t input = create_tensor(0);
    struct nexora_work_desc desc = {
        .struct_size = sizeof(desc), .op = NEXORA_OP_NOOP,
        .device_mask = NEXORA_DEVICE_CPU, .input_count = 1, .inputs = {input},
    };
    nexora_handle_t work = 0;
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_WORK_SUBMIT,
        (uintptr_t)&desc, (uintptr_t)&work, 0, 0, 0, 0), NEXORA_OK);
    struct nexora_work_result result = {.struct_size = sizeof(result)};
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_WORK_WAIT, work, 0,
        (uintptr_t)&result, 0, 0, 0), NEXORA_ERR(NEXORA_ETIMEDOUT));
}

static void test_capability_delegation_attenuates_rights(void) {
    reset_runtime();
    nexora_handle_t source = create_tensor(0);
    struct nexora_cap_delegate request = {
        .struct_size = sizeof(request),
        .target_pid = 2,
        .source_handle = source,
        .rights = NEXORA_RIGHT_READ | NEXORA_RIGHT_MAP,
    };
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_CAP_DELEGATE,
        (uintptr_t)&request, 0, 0, 0, 0, 0), NEXORA_OK);
    ASSERT_TRUE(request.delegated_handle != 0);

    void *object = NULL;
    ASSERT_EQ(nexora_handle_resolve(&p2.handles, request.delegated_handle,
        NEXORA_HANDLE_TENSOR, NEXORA_RIGHT_READ, &object, NULL), NEXORA_OK);
    ASSERT_EQ(nexora_handle_resolve(&p2.handles, request.delegated_handle,
        NEXORA_HANDLE_TENSOR, NEXORA_RIGHT_WRITE, &object, NULL), NEXORA_ERR(NEXORA_EACCES));

    request.rights = NEXORA_RIGHT_READ | NEXORA_RIGHT_WRITE | NEXORA_RIGHT_MAP |
                     NEXORA_RIGHT_DELEGATE | (1ull << 40);
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_CAP_DELEGATE,
        (uintptr_t)&request, 0, 0, 0, 0, 0), NEXORA_ERR(NEXORA_EACCES));
}

static void test_capability_delegation_output_fault_does_not_leak(void) {
    reset_runtime();
    nexora_handle_t source = create_tensor(0);
    struct nexora_cap_delegate request = {
        .struct_size = sizeof(request),
        .target_pid = 2,
        .source_handle = source,
        .rights = NEXORA_RIGHT_READ | NEXORA_RIGHT_MAP,
    };
    denied_write_start = (uintptr_t)&request;
    denied_write_end = denied_write_start + sizeof(request);
    nexora_uaccess_set_validator(selective_validator);
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_CAP_DELEGATE,
        (uintptr_t)&request, 0, 0, 0, 0, 0), NEXORA_ERR(NEXORA_EFAULT));
    nexora_uaccess_set_validator(permissive_validator);
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_TENSOR_RELEASE, source, 0, 0, 0, 0, 0), NEXORA_OK);
    ASSERT_EQ(mock_live_tensors(), 0);
}

static void test_device_query(void) {
    reset_runtime();
    struct nexora_device_info info = {.struct_size = sizeof(info)};
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_DEVICE_QUERY, 1,
        (uintptr_t)&info, 0, 0, 0, 0), NEXORA_OK);
    ASSERT_EQ(info.kind, NEXORA_DEVICE_GPU);
    ASSERT_TRUE(strstr(info.name, "GPU") != NULL);
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_DEVICE_QUERY, 99,
        (uintptr_t)&info, 0, 0, 0, 0), NEXORA_ERR(NEXORA_ENOENT));
}

static void test_bad_user_pointer_and_unknown_syscall(void) {
    reset_runtime();
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_ABI_QUERY, 0, 0, 0, 0, 0, 0), NEXORA_ERR(NEXORA_EFAULT));
    ASSERT_EQ(nexora_syscall_dispatch(0xFFFF, 0, 0, 0, 0, 0, 0), NEXORA_ERR(NEXORA_ENOSYS));
}

static void test_wrong_handle_type_rejected(void) {
    reset_runtime();
    nexora_handle_t tensor = create_tensor(0);
    struct nexora_work_desc desc = {
        .struct_size = sizeof(desc), .op = NEXORA_OP_NOOP,
        .device_mask = NEXORA_DEVICE_CPU, .input_count = 1, .inputs = {tensor},
    };
    nexora_handle_t work = 0;
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_WORK_SUBMIT,
        (uintptr_t)&desc, (uintptr_t)&work, 0, 0, 0, 0), NEXORA_OK);
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_TENSOR_RELEASE, work, 0, 0, 0, 0, 0), NEXORA_ERR(NEXORA_EINVAL));
}



static void test_process_cleanup_releases_delegated_resources(void) {
    reset_runtime();
    nexora_handle_t source = create_tensor(0);
    struct nexora_cap_delegate request = {
        .struct_size = sizeof(request),
        .target_pid = 2,
        .source_handle = source,
        .rights = NEXORA_RIGHT_READ | NEXORA_RIGHT_MAP,
    };
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_CAP_DELEGATE,
        (uintptr_t)&request, 0, 0, 0, 0, 0), NEXORA_OK);
    ASSERT_EQ(mock_live_tensors(), 1);
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_TENSOR_RELEASE, source, 0, 0, 0, 0, 0), NEXORA_OK);
    ASSERT_EQ(mock_live_tensors(), 1);
    nexora_syscall_process_cleanup(&p2);
    ASSERT_EQ(mock_live_tensors(), 0);
}

static void test_process_cleanup_unregisters_pid(void) {
    reset_runtime();
    ASSERT_TRUE(nexora_process_lookup(2) == &p2);
    nexora_syscall_set_current_process(&p2);
    nexora_syscall_process_cleanup(&p2);
    ASSERT_TRUE(nexora_process_lookup(2) == NULL);
    ASSERT_TRUE(nexora_syscall_current_process() == NULL);
    struct nexora_process replacement;
    ASSERT_EQ(nexora_process_init(&replacement, 2, 1, UINTPTR_MAX), NEXORA_OK);
    ASSERT_TRUE(nexora_process_lookup(2) == &replacement);
}

struct test_elf64_ehdr {
    unsigned char ident[16];
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
} __attribute__((packed));

struct test_elf64_phdr {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} __attribute__((packed));

struct map_capture {
    unsigned calls;
    uintptr_t address;
    size_t mem_size;
    size_t file_size;
    uint32_t flags;
};

static nexora_status_t capture_map_segment(struct nexora_process *process,
                                           uintptr_t virtual_address,
                                           size_t memory_size,
                                           const void *file_data,
                                           size_t file_size,
                                           uint32_t flags,
                                           void *context) {
    (void)process;
    ASSERT_TRUE(file_data != NULL);
    struct map_capture *capture = context;
    capture->calls++;
    capture->address = virtual_address;
    capture->mem_size = memory_size;
    capture->file_size = file_size;
    capture->flags = flags;
    return NEXORA_OK;
}

static void make_test_elf(unsigned char *image, size_t size, uint32_t ph_flags) {
    memset(image, 0, size);
    struct test_elf64_ehdr *eh = (struct test_elf64_ehdr *)image;
    eh->ident[0] = 0x7f; eh->ident[1] = 'E'; eh->ident[2] = 'L'; eh->ident[3] = 'F';
    eh->ident[4] = 2; eh->ident[5] = 1; eh->ident[6] = 1;
    eh->type = 2;
    eh->machine = 62;
    eh->version = 1;
    eh->entry = 0x400000;
    eh->phoff = sizeof(*eh);
    eh->ehsize = sizeof(*eh);
    eh->phentsize = sizeof(struct test_elf64_phdr);
    eh->phnum = 1;
    struct test_elf64_phdr *ph = (struct test_elf64_phdr *)(image + eh->phoff);
    ph->type = 1;
    ph->flags = ph_flags;
    ph->offset = 0x100;
    ph->vaddr = 0x400000;
    ph->filesz = 16;
    ph->memsz = 4096;
    ph->align = 1;
    for (unsigned i = 0; i < 16; ++i) image[0x100 + i] = (unsigned char)(0x90 + i);
}

static void test_elf_loader_validates_and_maps(void) {
    reset_runtime();
    unsigned char image[512];
    make_test_elf(image, sizeof(image), 4u | 1u); /* R|X */
    struct map_capture capture = {0};
    const struct nexora_user_vm_ops ops = {.map_segment = capture_map_segment};
    uintptr_t entry = 0;
    ASSERT_EQ(nexora_elf64_load(image, sizeof(image), &p1, &ops, &capture, &entry), NEXORA_OK);
    ASSERT_EQ(entry, 0x400000);
    ASSERT_EQ(capture.calls, 1);
    ASSERT_EQ(capture.address, 0x400000);
    ASSERT_EQ(capture.mem_size, 4096);
    ASSERT_EQ(capture.file_size, 16);
    ASSERT_TRUE((capture.flags & NEXORA_SEG_EXEC) != 0);
}

static void test_elf_loader_rejects_wx(void) {
    reset_runtime();
    unsigned char image[512];
    make_test_elf(image, sizeof(image), 4u | 2u | 1u); /* R|W|X */
    struct map_capture capture = {0};
    const struct nexora_user_vm_ops ops = {.map_segment = capture_map_segment};
    uintptr_t entry = 0;
    ASSERT_EQ(nexora_elf64_load(image, sizeof(image), &p1, &ops, &capture, &entry), NEXORA_ERR(NEXORA_EACCES));
    ASSERT_EQ(capture.calls, 0);
}

static void test_elf_loader_rejects_nonexec_entry(void) {
    reset_runtime();
    unsigned char image[512];
    make_test_elf(image, sizeof(image), 4u); /* R only */
    struct map_capture capture = {0};
    const struct nexora_user_vm_ops ops = {.map_segment = capture_map_segment};
    uintptr_t entry = 0;
    ASSERT_EQ(nexora_elf64_load(image, sizeof(image), &p1, &ops, &capture, &entry),
              NEXORA_ERR(NEXORA_EACCES));
    ASSERT_EQ(capture.calls, 0);
}

static bool test_fault_probe_reject_unmapped(uintptr_t addr, size_t len, bool write) {
    (void)len; (void)write;
    if (addr >= 0x2000 && addr < 0x3000) return false;
    return true;
}

static void test_uaccess_unmapped_page_fault_fixup(void) {
    reset_runtime();
    uint8_t kbuf[32] = {0};

    nexora_exception_fixup_register(0x2050, 0x2090);
    ASSERT_EQ(nexora_exception_fixup_lookup(0x2050), 0x2090);
    ASSERT_EQ(nexora_exception_fixup_lookup(0x1111), 0);

    nexora_uaccess_set_fault_probe(test_fault_probe_reject_unmapped);

    const void *unmapped_user_ptr = (const void *)(uintptr_t)0x2010;
    nexora_status_t status = nexora_copy_from_user(&p1, kbuf, unmapped_user_ptr, sizeof(kbuf));
    ASSERT_EQ(status, NEXORA_ERR(NEXORA_EFAULT));

    nexora_uaccess_set_fault_probe(NULL);
}

static void test_syscall_return_frame_rejects_non_canonical_rip(void) {
    reset_runtime();
    nexora_syscall_set_current_process(&p1);

    /* Valid lower-canonical user pointers */
    ASSERT_TRUE(nexora_is_canonical_user_rip(0x0000000000400000ull));
    ASSERT_TRUE(nexora_is_canonical_user_rip(0x00007fffffffffffull));

    /* Non-canonical addresses */
    ASSERT_TRUE(!nexora_is_canonical_user_rip(0x0000800000000000ull));
    ASSERT_TRUE(!nexora_is_canonical_user_rip(0x8000000000000000ull));

    /* High-half kernel addresses (forbidden for user sysret) */
    ASSERT_TRUE(!nexora_is_canonical_user_rip(0xffffffff80000000ull));
    ASSERT_TRUE(!nexora_is_canonical_user_rip(0xffffffffffffffffull));

    /* Craft a syscall return frame with a non-canonical RIP */
    struct nexora_syscall_frame frame = {
        .r9 = 0, .r8 = 0, .r10 = 0, .rdx = 0,
        .rsi = 0, .rdi = 0, .rax = 0,
        .user_rflags = 0x202,
        .user_rip = 0x8000000000000000ull
    };
    ASSERT_TRUE(!nexora_is_canonical_user_rip(frame.user_rip));

    /* Fault path terminates the offending process safely without executing sysret */
    ASSERT_TRUE(p1.alive);
    nexora_syscall_bad_rip_fault(&frame);
    ASSERT_TRUE(!p1.alive);

    nexora_syscall_set_current_process(NULL);
}

static void test_elf_loader_rejects_overlapping_segments(void) {
    reset_runtime();
    unsigned char image[1024];
    memset(image, 0, sizeof(image));
    struct test_elf64_ehdr *eh = (struct test_elf64_ehdr *)image;
    eh->ident[0] = 0x7f; eh->ident[1] = 'E'; eh->ident[2] = 'L'; eh->ident[3] = 'F';
    eh->ident[4] = 2; eh->ident[5] = 1; eh->ident[6] = 1;
    eh->type = 2;
    eh->machine = 62;
    eh->version = 1;
    eh->entry = 0x400000;
    eh->phoff = sizeof(*eh);
    eh->ehsize = sizeof(*eh);
    eh->phentsize = sizeof(struct test_elf64_phdr);
    eh->phnum = 2;

    struct test_elf64_phdr *ph1 = (struct test_elf64_phdr *)(image + eh->phoff);
    ph1->type = 1;
    ph1->flags = 4u | 1u;
    ph1->offset = 0x100;
    ph1->vaddr = 0x400000;
    ph1->filesz = 16;
    ph1->memsz = 4096;
    ph1->align = 1;

    struct test_elf64_phdr *ph2 = (struct test_elf64_phdr *)(image + eh->phoff + sizeof(struct test_elf64_phdr));
    ph2->type = 1;
    ph2->flags = 4u | 2u;
    ph2->offset = 0x110;
    ph2->vaddr = 0x400800;
    ph2->filesz = 16;
    ph2->memsz = 4096;
    ph2->align = 1;

    for (unsigned i = 0; i < 32; ++i) image[0x100 + i] = (unsigned char)(0x90 + i);

    struct map_capture capture = {0};
    const struct nexora_user_vm_ops ops = {.map_segment = capture_map_segment};
    uintptr_t entry = 0;
    ASSERT_EQ(nexora_elf64_load(image, sizeof(image), &p1, &ops, &capture, &entry),
              NEXORA_ERR(NEXORA_EINVAL));
    ASSERT_EQ(capture.calls, 0);
}

static void test_capability_delegation_rejects_unrelated_process(void) {
    reset_runtime();
    struct nexora_process p3;
    ASSERT_EQ(nexora_process_init(&p3, 3, 1, UINTPTR_MAX), NEXORA_OK);
    nexora_process_set_parent(&p3, 999);

    nexora_handle_t source = create_tensor(0);
    struct nexora_cap_delegate request = {
        .struct_size = sizeof(request),
        .target_pid = 3,
        .source_handle = source,
        .rights = NEXORA_RIGHT_READ,
    };
    ASSERT_EQ(nexora_syscall_dispatch(NEXORA_SYS_AI_CAP_DELEGATE,
        (uintptr_t)&request, 0, 0, 0, 0, 0), NEXORA_ERR(NEXORA_EPERM));

    (void)nexora_process_unregister(&p3);
}

static void test_percpu_stack_reentrancy_isolation(void) {
    reset_runtime();
    struct nexora_percpu cpu;
    nexora_percpu_init(&cpu, 0, 0x10000ull);
    ASSERT_EQ(cpu.kernel_rsp, 0x10000ull);
    ASSERT_EQ(cpu.user_rsp, 0ull);
    ASSERT_EQ(cpu.nest_depth, 0u);

    /* Thread syscall entry: user rsp = 0x5000 */
    ASSERT_EQ(nexora_percpu_push_user_rsp(&cpu, 0x5000ull), 0);
    ASSERT_EQ(cpu.user_rsp, 0x5000ull);
    ASSERT_EQ(cpu.nest_depth, 1u);

    /* Simulated interrupt / nested syscall re-entering same CPU: user rsp = 0x6000 */
    ASSERT_EQ(nexora_percpu_push_user_rsp(&cpu, 0x6000ull), 0);
    ASSERT_EQ(cpu.user_rsp, 0x6000ull);
    ASSERT_EQ(cpu.nest_depth, 2u);

    /* Return from nested syscall: pops 0x6000, restores 0x5000 */
    uint64_t popped1 = nexora_percpu_pop_user_rsp(&cpu);
    ASSERT_EQ(popped1, 0x6000ull);
    ASSERT_EQ(cpu.user_rsp, 0x5000ull);
    ASSERT_EQ(cpu.nest_depth, 1u);

    /* Return from thread syscall: pops 0x5000, restores 0 */
    uint64_t popped2 = nexora_percpu_pop_user_rsp(&cpu);
    ASSERT_EQ(popped2, 0x5000ull);
    ASSERT_EQ(cpu.user_rsp, 0ull);
    ASSERT_EQ(cpu.nest_depth, 0u);
}

int main(void) {
    test_abi_query();
    test_abi_query_without_backend();
    test_uaccess_requires_validator();
    test_uaccess_unmapped_page_fault_fixup();
    test_tensor_lifecycle_and_stale_handle();
    test_tensor_map_output_fault_has_no_backend_side_effect();
    test_readonly_rights_enforced();
    test_work_submit_wait();
    test_work_submit_output_fault_rolls_back();
    test_wait_timeout();
    test_capability_delegation_attenuates_rights();
    test_capability_delegation_output_fault_does_not_leak();
    test_capability_delegation_rejects_unrelated_process();
    test_device_query();
    test_bad_user_pointer_and_unknown_syscall();
    test_wrong_handle_type_rejected();
    test_process_cleanup_releases_delegated_resources();
    test_process_cleanup_unregisters_pid();
    test_elf_loader_validates_and_maps();
    test_elf_loader_rejects_wx();
    test_elf_loader_rejects_nonexec_entry();
    test_elf_loader_rejects_overlapping_segments();
    test_syscall_return_frame_rejects_non_canonical_rip();
    test_percpu_stack_reentrancy_isolation();
    puts("Phase 5 host tests: PASS (24 test groups)");
    return 0;
}
