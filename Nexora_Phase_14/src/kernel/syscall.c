#include <nexora/syscall.h>
#include <nexora/backend.h>
#include <nexora/uaccess.h>
#include <nexora/handle.h>
#include <stddef.h>

_Static_assert(offsetof(struct nexora_syscall_frame, r9) == 0, "syscall frame r9 offset");
_Static_assert(offsetof(struct nexora_syscall_frame, r8) == 8, "syscall frame r8 offset");
_Static_assert(offsetof(struct nexora_syscall_frame, r10) == 16, "syscall frame r10 offset");
_Static_assert(offsetof(struct nexora_syscall_frame, rdx) == 24, "syscall frame rdx offset");
_Static_assert(offsetof(struct nexora_syscall_frame, rsi) == 32, "syscall frame rsi offset");
_Static_assert(offsetof(struct nexora_syscall_frame, rdi) == 40, "syscall frame rdi offset");
_Static_assert(offsetof(struct nexora_syscall_frame, rax) == 48, "syscall frame rax offset");
_Static_assert(offsetof(struct nexora_syscall_frame, user_rflags) == 56, "syscall frame rflags offset");
_Static_assert(offsetof(struct nexora_syscall_frame, user_rip) == 64, "syscall frame rip offset");
_Static_assert(sizeof(struct nexora_syscall_frame) == 72, "syscall frame size");

static struct nexora_process *current_process;

static void zero_bytes(void *ptr, size_t length) {
    unsigned char *p = (unsigned char *)ptr;
    for (size_t i = 0; i < length; ++i) p[i] = 0;
}

void nexora_syscall_set_current_process(struct nexora_process *process) {
    current_process = process;
}

struct nexora_process *nexora_syscall_current_process(void) {
    return current_process;
}

static void release_resource(const struct nexora_backend_ops *ops,
                             struct nexora_process *process,
                             uint8_t type,
                             void *object) {
    if (!ops || !object) return;
    if (type == NEXORA_HANDLE_TENSOR && ops->tensor_release) {
        (void)ops->tensor_release(process, object);
    } else if (type == NEXORA_HANDLE_WORK && ops->work_release) {
        (void)ops->work_release(process, object);
    }
}

void nexora_syscall_process_cleanup(struct nexora_process *process) {
    if (!process) return;
    const struct nexora_backend_ops *ops = nexora_backend_get();
    for (uint32_t i = 0; i < NEXORA_MAX_HANDLES; ++i) {
        struct nexora_handle_slot *slot = &process->handles.slots[i];
        if (!slot->occupied) continue;
        release_resource(ops, process, slot->type, slot->object);
        slot->object = NULL;
        slot->rights = 0;
        slot->type = NEXORA_HANDLE_NONE;
        slot->occupied = 0;
        slot->generation = (uint16_t)(slot->generation + 1u);
        if (slot->generation == 0) slot->generation = 1;
    }
    if (current_process == process) current_process = NULL;
    (void)nexora_process_unregister(process);
}

static nexora_status_t require_process(struct nexora_process **process_out) {
    struct nexora_process *process = current_process;
    if (!process || !process->alive) return NEXORA_ERR(NEXORA_EPERM);
    *process_out = process;
    return NEXORA_OK;
}

static bool tensor_desc_valid(const struct nexora_tensor_desc *desc) {
    if (!desc || desc->struct_size < sizeof(*desc)) return false;
    if (desc->dtype > NEXORA_DTYPE_I32) return false;
    if (desc->ndim == 0 || desc->ndim > NEXORA_ABI_MAX_DIMS) return false;
    if (desc->location > NEXORA_LOC_REMOTE) return false;
    for (uint32_t i = 0; i < desc->ndim; ++i) {
        if (desc->shape[i] == 0) return false;
    }
    return true;
}

static nexora_status_t sys_abi_query(struct nexora_process *process, uint64_t user_info) {
    struct nexora_abi_info info = {
        .abi_version = NEXORA_ABI_VERSION,
        .syscall_count = NEXORA_SYS_MAX,
        .pointer_bits = (uint32_t)(8u * sizeof(uintptr_t)),
        .reserved = 0,
    };
    return nexora_copy_to_user(process, (void *)(uintptr_t)user_info, &info, sizeof(info));
}

static nexora_status_t sys_tensor_create(struct nexora_process *process,
                                         const struct nexora_backend_ops *ops,
                                         uint64_t user_desc,
                                         uint64_t user_handle) {
    if (!ops->tensor_create || !ops->tensor_release) return NEXORA_ERR(NEXORA_ENOSYS);
    struct nexora_tensor_desc desc;
    nexora_status_t status = nexora_copy_from_user(process, &desc,
        (const void *)(uintptr_t)user_desc, sizeof(desc));
    if (status != NEXORA_OK) return status;
    if (!tensor_desc_valid(&desc)) return NEXORA_ERR(NEXORA_EINVAL);
    if (!nexora_uaccess_range_ok(process, (void *)(uintptr_t)user_handle,
                                 sizeof(nexora_handle_t), true)) {
        return NEXORA_ERR(NEXORA_EFAULT);
    }

    void *object = NULL;
    uint64_t rights = 0;
    status = ops->tensor_create(process, &desc, &object, &rights);
    if (status != NEXORA_OK) return status;
    nexora_handle_t handle = NEXORA_INVALID_HANDLE;
    status = nexora_handle_alloc(&process->handles, NEXORA_HANDLE_TENSOR, object, rights, &handle);
    if (status != NEXORA_OK) {
        if (ops->tensor_release) (void)ops->tensor_release(process, object);
        return status;
    }
    status = nexora_copy_to_user(process, (void *)(uintptr_t)user_handle, &handle, sizeof(handle));
    if (status != NEXORA_OK) {
        void *removed = NULL;
        (void)nexora_handle_remove(&process->handles, handle, NEXORA_HANDLE_TENSOR, &removed);
        if (ops->tensor_release && removed) (void)ops->tensor_release(process, removed);
    }
    return status;
}

static nexora_status_t sys_tensor_map(struct nexora_process *process,
                                      const struct nexora_backend_ops *ops,
                                      uint64_t handle_value,
                                      uint64_t user_request,
                                      uint64_t user_address) {
    if (!ops->tensor_map) return NEXORA_ERR(NEXORA_ENOSYS);
    struct nexora_tensor_map request;
    nexora_status_t status = nexora_copy_from_user(process, &request,
        (const void *)(uintptr_t)user_request, sizeof(request));
    if (status != NEXORA_OK) return status;
    if (request.struct_size < sizeof(request)) return NEXORA_ERR(NEXORA_EINVAL);
    if ((request.flags & ~(uint32_t)(NEXORA_MAP_READ | NEXORA_MAP_WRITE)) != 0) return NEXORA_ERR(NEXORA_EINVAL);
    if ((request.flags & NEXORA_MAP_READ) == 0) return NEXORA_ERR(NEXORA_EINVAL);
    if (!nexora_uaccess_range_ok(process, (void *)(uintptr_t)user_address,
                                 sizeof(uintptr_t), true)) {
        return NEXORA_ERR(NEXORA_EFAULT);
    }

    uint64_t required = NEXORA_RIGHT_MAP | NEXORA_RIGHT_READ;
    if (request.flags & NEXORA_MAP_WRITE) required |= NEXORA_RIGHT_WRITE;
    void *object = NULL;
    status = nexora_handle_resolve(&process->handles, (nexora_handle_t)handle_value,
                                   NEXORA_HANDLE_TENSOR, required, &object, NULL);
    if (status != NEXORA_OK) return status;

    uintptr_t address = 0;
    status = ops->tensor_map(process, object, &request, &address);
    if (status != NEXORA_OK) return status;
    return nexora_copy_to_user(process, (void *)(uintptr_t)user_address, &address, sizeof(address));
}

static nexora_status_t sys_tensor_release(struct nexora_process *process,
                                          const struct nexora_backend_ops *ops,
                                          uint64_t handle_value) {
    if (!ops->tensor_release) return NEXORA_ERR(NEXORA_ENOSYS);
    void *object = NULL;
    nexora_status_t status = nexora_handle_resolve(&process->handles,
        (nexora_handle_t)handle_value, NEXORA_HANDLE_TENSOR,
        0, &object, NULL);
    if (status != NEXORA_OK) return status;
    status = ops->tensor_release(process, object);
    if (status != NEXORA_OK) return status;
    return nexora_handle_remove(&process->handles, (nexora_handle_t)handle_value,
                                NEXORA_HANDLE_TENSOR, NULL);
}

static nexora_status_t sys_work_submit(struct nexora_process *process,
                                       const struct nexora_backend_ops *ops,
                                       uint64_t user_desc,
                                       uint64_t user_handle) {
    if (!ops->work_submit || !ops->work_release) return NEXORA_ERR(NEXORA_ENOSYS);
    struct nexora_work_desc desc;
    nexora_status_t status = nexora_copy_from_user(process, &desc,
        (const void *)(uintptr_t)user_desc, sizeof(desc));
    if (status != NEXORA_OK) return status;
    if (desc.struct_size < sizeof(desc) || desc.op > NEXORA_OP_CUSTOM ||
        desc.input_count > NEXORA_ABI_MAX_IO_TENSORS ||
        desc.output_count > NEXORA_ABI_MAX_IO_TENSORS) {
        return NEXORA_ERR(NEXORA_EINVAL);
    }
    if (!nexora_uaccess_range_ok(process, (void *)(uintptr_t)user_handle,
                                 sizeof(nexora_handle_t), true)) {
        return NEXORA_ERR(NEXORA_EFAULT);
    }

    void *inputs[NEXORA_ABI_MAX_IO_TENSORS] = {0};
    void *outputs[NEXORA_ABI_MAX_IO_TENSORS] = {0};
    for (uint32_t i = 0; i < desc.input_count; ++i) {
        status = nexora_handle_resolve(&process->handles, desc.inputs[i], NEXORA_HANDLE_TENSOR,
                                       NEXORA_RIGHT_READ, &inputs[i], NULL);
        if (status != NEXORA_OK) return status;
    }
    for (uint32_t i = 0; i < desc.output_count; ++i) {
        status = nexora_handle_resolve(&process->handles, desc.outputs[i], NEXORA_HANDLE_TENSOR,
                                       NEXORA_RIGHT_WRITE, &outputs[i], NULL);
        if (status != NEXORA_OK) return status;
    }

    void *work_object = NULL;
    status = ops->work_submit(process, &desc, inputs, outputs, &work_object);
    if (status != NEXORA_OK) return status;

    nexora_handle_t work_handle;
    const uint64_t rights = NEXORA_RIGHT_WAIT | NEXORA_RIGHT_DELEGATE;
    status = nexora_handle_alloc(&process->handles, NEXORA_HANDLE_WORK, work_object, rights, &work_handle);
    if (status != NEXORA_OK) {
        if (ops->work_release) (void)ops->work_release(process, work_object);
        return status;
    }
    status = nexora_copy_to_user(process, (void *)(uintptr_t)user_handle, &work_handle, sizeof(work_handle));
    if (status != NEXORA_OK) {
        void *removed = NULL;
        (void)nexora_handle_remove(&process->handles, work_handle, NEXORA_HANDLE_WORK, &removed);
        if (ops->work_release && removed) (void)ops->work_release(process, removed);
    }
    return status;
}

static nexora_status_t sys_work_wait(struct nexora_process *process,
                                     const struct nexora_backend_ops *ops,
                                     uint64_t handle_value,
                                     uint64_t timeout_ns,
                                     uint64_t user_result) {
    if (!ops->work_wait || !ops->work_release) return NEXORA_ERR(NEXORA_ENOSYS);
    void *object = NULL;
    nexora_status_t status = nexora_handle_resolve(&process->handles,
        (nexora_handle_t)handle_value, NEXORA_HANDLE_WORK, NEXORA_RIGHT_WAIT, &object, NULL);
    if (status != NEXORA_OK) return status;
    if (!nexora_uaccess_range_ok(process, (void *)(uintptr_t)user_result,
                                 sizeof(struct nexora_work_result), true)) {
        return NEXORA_ERR(NEXORA_EFAULT);
    }
    struct nexora_work_result result;
    zero_bytes(&result, sizeof(result));
    result.struct_size = sizeof(result);
    status = ops->work_wait(process, object, timeout_ns, &result);
    if (status != NEXORA_OK) return status;
    status = nexora_copy_to_user(process, (void *)(uintptr_t)user_result, &result, sizeof(result));
    if (status != NEXORA_OK) return status;

    if (result.state == NEXORA_WORK_DONE || result.state == NEXORA_WORK_FAILED) {
        status = ops->work_release(process, object);
        if (status != NEXORA_OK) return status;
        return nexora_handle_remove(&process->handles, (nexora_handle_t)handle_value,
                                    NEXORA_HANDLE_WORK, NULL);
    }
    return NEXORA_OK;
}

static nexora_status_t sys_cap_delegate(struct nexora_process *process,
                                        const struct nexora_backend_ops *ops,
                                        uint64_t user_delegate) {
    if (!ops->retain) return NEXORA_ERR(NEXORA_ENOSYS);
    struct nexora_cap_delegate request;
    nexora_status_t status = nexora_copy_from_user(process, &request,
        (const void *)(uintptr_t)user_delegate, sizeof(request));
    if (status != NEXORA_OK) return status;
    if (request.struct_size < sizeof(request) || request.target_pid == 0 || request.rights == 0) {
        return NEXORA_ERR(NEXORA_EINVAL);
    }
    if (!nexora_uaccess_range_ok(process, (void *)(uintptr_t)user_delegate,
                                 sizeof(struct nexora_cap_delegate), true)) {
        return NEXORA_ERR(NEXORA_EFAULT);
    }

    void *object = NULL;
    uint64_t source_rights = 0;
    status = nexora_handle_resolve(&process->handles, request.source_handle, NEXORA_HANDLE_NONE,
                                   NEXORA_RIGHT_DELEGATE, &object, &source_rights);
    if (status != NEXORA_OK) return status;
    if ((request.rights & source_rights) != request.rights) return NEXORA_ERR(NEXORA_EACCES);

    struct nexora_process *target = nexora_process_lookup(request.target_pid);
    if (!target) return NEXORA_ERR(NEXORA_ENOENT);
    if (target->pid != process->pid && target->parent_pid != process->pid) {
        return NEXORA_ERR(NEXORA_EPERM);
    }
    uint8_t type = (uint8_t)((request.source_handle >> 32u) & 0xFFu);
    status = ops->retain(type, object);
    if (status != NEXORA_OK) return status;
    nexora_handle_t delegated = NEXORA_INVALID_HANDLE;
    status = nexora_handle_alloc(&target->handles, type, object, request.rights, &delegated);
    if (status != NEXORA_OK) {
        release_resource(ops, target, type, object);
        return status;
    }
    request.delegated_handle = delegated;
    status = nexora_copy_to_user(process, (void *)(uintptr_t)user_delegate, &request, sizeof(request));
    if (status != NEXORA_OK) {
        void *removed = NULL;
        (void)nexora_handle_remove(&target->handles, delegated, type, &removed);
        if (removed) release_resource(ops, target, type, removed);
    }
    return status;
}

static nexora_status_t sys_device_query(struct nexora_process *process,
                                        const struct nexora_backend_ops *ops,
                                        uint64_t ordinal,
                                        uint64_t user_info) {
    if (!ops->device_query) return NEXORA_ERR(NEXORA_ENOSYS);
    if (!nexora_uaccess_range_ok(process, (void *)(uintptr_t)user_info,
                                 sizeof(struct nexora_device_info), true)) {
        return NEXORA_ERR(NEXORA_EFAULT);
    }
    struct nexora_device_info info;
    zero_bytes(&info, sizeof(info));
    info.struct_size = sizeof(info);
    nexora_status_t status = ops->device_query(process, (uint32_t)ordinal, &info);
    if (status != NEXORA_OK) return status;
    return nexora_copy_to_user(process, (void *)(uintptr_t)user_info, &info, sizeof(info));
}

nexora_status_t nexora_syscall_dispatch(uint64_t number,
                                        uint64_t a0,
                                        uint64_t a1,
                                        uint64_t a2,
                                        uint64_t a3,
                                        uint64_t a4,
                                        uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    struct nexora_process *process = NULL;
    nexora_status_t status = require_process(&process);
    if (status != NEXORA_OK) return status;
    if (number == NEXORA_SYS_ABI_QUERY) return sys_abi_query(process, a0);

    const struct nexora_backend_ops *ops = nexora_backend_get();
    if (!ops) return NEXORA_ERR(NEXORA_ENOSYS);

    switch (number) {
        case NEXORA_SYS_AI_TENSOR_CREATE: return sys_tensor_create(process, ops, a0, a1);
        case NEXORA_SYS_AI_TENSOR_MAP: return sys_tensor_map(process, ops, a0, a1, a2);
        case NEXORA_SYS_AI_TENSOR_RELEASE: return sys_tensor_release(process, ops, a0);
        case NEXORA_SYS_AI_WORK_SUBMIT: return sys_work_submit(process, ops, a0, a1);
        case NEXORA_SYS_AI_WORK_WAIT: return sys_work_wait(process, ops, a0, a1, a2);
        case NEXORA_SYS_AI_CAP_DELEGATE: return sys_cap_delegate(process, ops, a0);
        case NEXORA_SYS_AI_DEVICE_QUERY: return sys_device_query(process, ops, a0, a1);
        default: return NEXORA_ERR(NEXORA_ENOSYS);
    }
}

void nexora_syscall_dispatch_frame(struct nexora_syscall_frame *frame) {
    if (!frame) return;
    frame->rax = (uint64_t)nexora_syscall_dispatch(frame->rax, frame->rdi, frame->rsi,
                                                   frame->rdx, frame->r10, frame->r8, frame->r9);
}

void nexora_syscall_bad_rip_fault(struct nexora_syscall_frame *frame) {
    (void)frame;
    struct nexora_process *process = current_process;
    if (process) {
        process->alive = false;
        nexora_syscall_process_cleanup(process);
    }
}
