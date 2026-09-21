# Nexora Phase-5 ABI Specification

## Calling convention

For x86-64:

```text
rax : syscall number
rdi : argument 0
rsi : argument 1
rdx : argument 2
r10 : argument 3
r8  : argument 4
r9  : argument 5
rax : return value
```

Return values are signed 64-bit values. `0` means success unless a syscall explicitly returns a scalar. Errors are negative `NEXORA_E*` values.

## Syscall numbers

| Number | Name | Purpose |
|---:|---|---|
| 0 | `nexora_abi_query` | ABI version/capability discovery |
| 1 | `ai_tensor_create` | Create tensor object and process handle |
| 2 | `ai_tensor_map` | Map tensor backing into caller's address space |
| 3 | `ai_tensor_release` | Close caller's tensor handle/reference |
| 4 | `ai_work_submit` | Submit graph work using tensor handles |
| 5 | `ai_work_wait` | Wait for a submitted work object |
| 6 | `ai_cap_delegate` | Duplicate a handle into another process with attenuated rights |
| 7 | `ai_device_query` | Enumerate Nexora compute devices |

## Handle format

The current 64-bit handle encoding is intentionally opaque to userspace. Kernel-side layout is:

```text
bits  0..15  : slot index + 1
bits 16..31  : generation
bits 32..39  : object type
bits 40..63  : reserved
```

Userspace must never decode or synthesize handles. The generation component prevents a closed handle value from silently referring to a later object reusing the same table slot.

## Tensor create

Input: `struct nexora_tensor_desc *`

Output: `nexora_handle_t *`

Validation includes dtype, rank, nonzero dimensions, location, user-pointer validity, and backend allocation success.

## Tensor map

Inputs:

```text
nexora_handle_t handle
struct nexora_tensor_map *request
uintptr_t *address_out
```

The Phase-4 backend performs the actual virtual mapping. `NEXORA_MAP_WRITE` requires write permission on the handle. The returned address is a userspace VA, not a physical address.

## Work submit and wait

`ai_work_submit` takes `struct nexora_work_desc *` and returns a work handle. Each input handle is resolved as a tensor with read permission; each output handle is resolved as a tensor with write permission. This prevents an arbitrary integer or a work handle from being passed as a tensor reference.

`ai_work_wait` waits on a work handle. Timeouts leave the handle valid. A successful terminal result (`NEXORA_WORK_DONE` or `NEXORA_WORK_FAILED`) consumes the caller's work-handle reference after copying the result to userspace. This provides a lifecycle path without adding a separate POSIX-like close syscall for work objects.

## Capability delegation

`ai_cap_delegate` performs **attenuation only**:

```text
delegated_rights ⊆ source_rights
```

The source handle must also carry `NEXORA_RIGHT_DELEGATE`. The target receives a different process-local handle referring to the same underlying object. The backend retain hook is required and must increase the object's reference count before the target handle is installed.

## ABI evolution rule

Every compound ABI structure begins with `struct_size`. New fields may only be appended. Existing field meaning, offsets, syscall numbers, and error semantics must remain stable within ABI major version 1.


## User-memory contract

All nonzero-length user-memory accesses are fail-closed until `nexora_uaccess_set_validator()` installs the Phase-4 page-table validator. Side-effecting syscalls prevalidate their output buffers before allocation, mapping, waiting, or delegation where practical; rollback paths handle failures that can still occur after the initial validation.
