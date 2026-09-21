# Integrating Phase 5 into the Phase-4 Nexora Tree

Phase 5 is designed as a narrow layer over the Phase-4 VM, tensor, graph, scheduler, and zero-copy sharing implementation. Do not duplicate those subsystems.

## 1. Add Phase-5 sources to the kernel build

Add these kernel objects:

```text
kernel/backend.c
kernel/elf64.c
kernel/handle.c
kernel/process.c
kernel/syscall.c
kernel/uaccess.c
arch/x86_64/gdt.c
arch/x86_64/syscall_msr.c
arch/x86_64/syscall_entry.S
arch/x86_64/usermode.S
```

Add `include/` to the include path.

## 2. Install the Phase-4 backend adapter

At boot, construct `struct nexora_backend_ops` and call:

```c
nexora_backend_install(&phase4_ops);
```

Wire operations as follows:

```text
tensor_create  -> Phase-4 tensor allocator/object constructor
tensor_map     -> Phase-4 zero-copy user mapping path
tensor_release -> Phase-4 tensor refcount release
retain         -> Phase-4 object refcount increment
work_submit    -> Phase-4 work-graph/scheduler submission
work_wait      -> Phase-4 completion/wait primitive
work_release   -> Phase-4 work-object release
device_query   -> Phase-4 simulated/registered device table
```

Do not implement `tensor_map` as memcpy. It must map the existing physical tensor backing into the caller's address space with the requested permissions.

## 3. Connect user-pointer validation to Phase-4 page tables

Install a validator:

```c
nexora_uaccess_set_validator(phase4_user_range_validate);
```

The validator should walk the caller's page tables and ensure every page in the range is:

- present,
- marked userspace-accessible,
- writable for copy-to-user,
- free of arithmetic wraparound,
- within the process's permitted user VA interval.

The generic Phase-5 bounds check is not a substitute for this page-table validation. Nonzero-length user copies fail with `-NEXORA_EFAULT` until this validator is installed.

## 4. Initialize GDT/TSS and syscall MSRs

After the kernel stack exists and before entering userspace:

```c
nexora_x86_gdt_init(kernel_stack_top);
nexora_x86_syscall_init(kernel_stack_top);
```

The Phase-5 syscall entry is a single-CPU bootstrap implementation. `nexora_x86_syscall_init()` aligns the bootstrap syscall stack down to a 16-byte boundary for the C ABI. On SMP, replace global syscall stack state with per-CPU storage before enabling concurrent userspace.

## 5. Create the first process

Example process VA policy:

```text
user_lo = 0x0000000040000000
user_hi = 0x00007ffffffff000
```

Then:

```c
nexora_process_system_init();
nexora_process_init(&init_process, 1, user_lo, user_hi);
nexora_syscall_set_current_process(&init_process);
```

For a real scheduler, `nexora_syscall_set_current_process()` must be switched during context changes or replaced with a per-CPU current-task accessor.

## 6. Load a user ELF

Map `build/user/demo.elf` using:

```c
struct nexora_user_vm_ops vm_ops = {
    .map_segment = phase4_map_elf_segment,
};

uintptr_t entry;
nexora_elf64_load(image, image_size, &init_process,
                  &vm_ops, vm_context, &entry);
```

`map_segment` must:

1. allocate/map user pages,
2. copy `file_size` bytes from the ELF image,
3. zero `memory_size - file_size` bytes,
4. install final R/W/X permissions,
5. flush TLB entries as required.

The loader rejects writable+executable load segments and requires the ELF entry point to lie inside an executable load segment. It performs structural validation before the first mapping callback. If a Phase-4 mapping callback subsequently fails mid-load, discard the partially constructed process/address space (or wrap mappings transactionally); the Phase-5 VM callback interface does not include per-segment rollback.

## 7. Map a user stack

Use the Phase-4 VM to allocate a user stack, preferably with at least one non-present guard page below it. Example:

```text
stack top: 0x00007fffffffe000
stack size: 64 KiB
lower guard: 4 KiB
```

## 8. Enter CPL3

Call:

```c
nexora_x86_enter_user(entry, user_stack_top);
```

The demo should issue `nexora_abi_query`, create/map a tensor, submit/wait for a no-op work item, and release the tensor. A terminal `ai_work_wait` consumes that process's work-handle reference.

## 9. Context-switch obligations

Before Phase 5 is considered scheduler-integrated, a context switch must update:

- CR3 / process address space,
- current process pointer,
- TSS `RSP0`,
- syscall kernel stack pointer,
- architecture state required by Phase-4 scheduling.

## 10. Process teardown

Before destroying the address space:

```c
nexora_syscall_process_cleanup(process);
```

`nexora_syscall_process_cleanup()` also unregisters the PID and clears the current-process pointer when cleaning the current task. Then unmap userspace pages and release the Phase-4 process/address-space object. Cleanup prevents delegated/shared tensor references and work handles from leaking across process death.
