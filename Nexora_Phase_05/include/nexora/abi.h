#ifndef NEXORA_ABI_H
#define NEXORA_ABI_H

#include <stdint.h>
#include <stddef.h>

#define NEXORA_ABI_VERSION_MAJOR 1u
#define NEXORA_ABI_VERSION_MINOR 0u
#define NEXORA_ABI_VERSION ((NEXORA_ABI_VERSION_MAJOR << 16) | NEXORA_ABI_VERSION_MINOR)
#define NEXORA_ABI_MAX_DIMS 8u
#define NEXORA_ABI_MAX_IO_TENSORS 8u

typedef uint64_t nexora_handle_t;
typedef int64_t nexora_status_t;

#define NEXORA_INVALID_HANDLE ((nexora_handle_t)0)

enum nexora_errno {
    NEXORA_OK          = 0,
    NEXORA_EPERM       = 1,
    NEXORA_ENOENT      = 2,
    NEXORA_EIO         = 5,
    NEXORA_ENOMEM      = 12,
    NEXORA_EACCES      = 13,
    NEXORA_EFAULT      = 14,
    NEXORA_EBUSY       = 16,
    NEXORA_EEXIST      = 17,
    NEXORA_EINVAL      = 22,
    NEXORA_ENOSPC      = 28,
    NEXORA_ENOSYS      = 38,
    NEXORA_ETIMEDOUT   = 110,
    NEXORA_ESTALE      = 116,
};

#define NEXORA_ERR(e) (-(nexora_status_t)(e))

enum nexora_syscall_number {
    NEXORA_SYS_ABI_QUERY = 0,
    NEXORA_SYS_AI_TENSOR_CREATE = 1,
    NEXORA_SYS_AI_TENSOR_MAP = 2,
    NEXORA_SYS_AI_TENSOR_RELEASE = 3,
    NEXORA_SYS_AI_WORK_SUBMIT = 4,
    NEXORA_SYS_AI_WORK_WAIT = 5,
    NEXORA_SYS_AI_CAP_DELEGATE = 6,
    NEXORA_SYS_AI_DEVICE_QUERY = 7,
    NEXORA_SYS_MAX = 8,
};

enum nexora_dtype {
    NEXORA_DTYPE_F32 = 0,
    NEXORA_DTYPE_F16 = 1,
    NEXORA_DTYPE_BF16 = 2,
    NEXORA_DTYPE_I8 = 3,
    NEXORA_DTYPE_I32 = 4,
};

enum nexora_tensor_location {
    NEXORA_LOC_CPU_RAM = 0,
    NEXORA_LOC_GPU_HBM = 1,
    NEXORA_LOC_NPU_MEM = 2,
    NEXORA_LOC_NVME = 3,
    NEXORA_LOC_REMOTE = 4,
};

enum nexora_tensor_flags {
    NEXORA_TENSOR_PERSISTENT = 1u << 0,
    NEXORA_TENSOR_EPHEMERAL = 1u << 1,
    NEXORA_TENSOR_READONLY = 1u << 2,
    NEXORA_TENSOR_PINNED = 1u << 3,
    NEXORA_TENSOR_ZEROED = 1u << 4,
};

enum nexora_map_flags {
    NEXORA_MAP_READ = 1u << 0,
    NEXORA_MAP_WRITE = 1u << 1,
};

enum nexora_rights {
    NEXORA_RIGHT_READ = 1ull << 0,
    NEXORA_RIGHT_WRITE = 1ull << 1,
    NEXORA_RIGHT_MAP = 1ull << 2,
    NEXORA_RIGHT_WAIT = 1ull << 3,
    NEXORA_RIGHT_SUBMIT = 1ull << 4,
    NEXORA_RIGHT_QUERY = 1ull << 5,
    NEXORA_RIGHT_DELEGATE = 1ull << 6,
    NEXORA_RIGHT_RELEASE = 1ull << 7,
};

enum nexora_device_kind {
    NEXORA_DEVICE_CPU = 1u << 0,
    NEXORA_DEVICE_GPU = 1u << 1,
    NEXORA_DEVICE_NPU = 1u << 2,
    NEXORA_DEVICE_NIC = 1u << 3,
};

enum nexora_work_op {
    NEXORA_OP_NOOP = 0,
    NEXORA_OP_MATMUL = 1,
    NEXORA_OP_ATTENTION = 2,
    NEXORA_OP_ACTIVATION = 3,
    NEXORA_OP_NORMALIZATION = 4,
    NEXORA_OP_EMBEDDING = 5,
    NEXORA_OP_TRANSFER = 6,
    NEXORA_OP_CUSTOM = 7,
};

enum nexora_work_state {
    NEXORA_WORK_PENDING = 0,
    NEXORA_WORK_READY = 1,
    NEXORA_WORK_RUNNING = 2,
    NEXORA_WORK_DONE = 3,
    NEXORA_WORK_FAILED = 4,
};

struct nexora_abi_info {
    uint32_t abi_version;
    uint32_t syscall_count;
    uint32_t pointer_bits;
    uint32_t reserved;
};

struct nexora_tensor_desc {
    uint32_t struct_size;
    uint32_t dtype;
    uint32_t ndim;
    uint32_t location;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t shape[NEXORA_ABI_MAX_DIMS];
};

struct nexora_tensor_map {
    uint32_t struct_size;
    uint32_t flags;
    uint64_t offset;
    uint64_t length;
    uint64_t address_hint;
};

struct nexora_work_desc {
    uint32_t struct_size;
    uint32_t op;
    uint32_t priority;
    uint32_t device_mask;
    uint64_t deadline_ns;
    uint64_t batch_id;
    uint32_t input_count;
    uint32_t output_count;
    nexora_handle_t inputs[NEXORA_ABI_MAX_IO_TENSORS];
    nexora_handle_t outputs[NEXORA_ABI_MAX_IO_TENSORS];
};

struct nexora_work_result {
    uint32_t struct_size;
    uint32_t state;
    int32_t device_id;
    int32_t completion_code;
    uint64_t started_ns;
    uint64_t completed_ns;
};

struct nexora_cap_delegate {
    uint32_t struct_size;
    uint32_t target_pid;
    nexora_handle_t source_handle;
    uint64_t rights;
    nexora_handle_t delegated_handle;
};

struct nexora_device_info {
    uint32_t struct_size;
    uint32_t id;
    uint32_t kind;
    uint32_t numa_node;
    uint64_t memory_bytes;
    uint64_t feature_bits;
    char name[48];
};

#endif
