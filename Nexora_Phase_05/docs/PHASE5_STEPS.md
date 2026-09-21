# Phase 5 — Complete Step Log

## Step 1 — Freeze the ABI contract

Implemented a versioned 64-bit ABI with fixed-width structures, explicit `struct_size` fields for forward compatibility, stable syscall numbers, negative errno-style results, and opaque `nexora_handle_t` resource identifiers.

Primary file: `include/nexora/abi.h`.

## Step 2 — Add process isolation primitives

Implemented a minimal process object with PID, allowed userspace virtual-address interval, and a per-process handle table. Handles encode index, generation, and resource type. Generation changes on removal so stale handles fail deterministically.

Primary files: `include/nexora/process.h`, `include/nexora/handle.h`, `kernel/process.c`, `kernel/handle.c`.

## Step 3 — Build the ring-3 execution substrate

Added a 64-bit GDT containing kernel/user code and data descriptors, a TSS with `RSP0`, and an `IRETQ` transition primitive for entering CPL3.

Primary files: `arch/x86_64/gdt.c`, `arch/x86_64/usermode.S`.

## Step 4 — Add an ELF64 userspace loader

Implemented a strict x86-64 `ET_EXEC` loader with program-header bounds checking, user-range enforcement, `filesz <= memsz` validation, and W^X rejection. Actual page allocation/mapping remains delegated to the Phase-4 VM implementation through `nexora_user_vm_ops`.

Primary files: `include/nexora/elf64.h`, `kernel/elf64.c`.

## Step 5 — Install the x86-64 syscall gate

Enabled `EFER.SCE`, programmed `STAR`, `LSTAR`, and `FMASK`, and added a `SYSCALL/SYSRET` entry path using the Nexora register ABI:

```text
rax = syscall number
rdi,rsi,rdx,r10,r8,r9 = args 0..5
rax = signed result
```

Primary files: `arch/x86_64/syscall_msr.c`, `arch/x86_64/syscall_entry.S`.

## Step 6 — Add safe user-access plumbing

Added copy-in/copy-out helpers that first verify process bounds and then invoke the required Phase-4 page-table validator. User access is fail-closed when no validator is installed. The validator must confirm presence, CPL3 accessibility, and write permission where required.

Primary files: `include/nexora/uaccess.h`, `kernel/uaccess.c`.

## Step 7 — Expose tensor syscalls

Implemented:

```text
ai_tensor_create
ai_tensor_map
ai_tensor_release
```

The dispatcher validates descriptors, maps handles to Phase-4 tensor objects, enforces read/write/map rights, and rejects stale or wrong-type handles.

Primary file: `kernel/syscall.c`.

## Step 8 — Expose work and device syscalls

Implemented:

```text
ai_work_submit
ai_work_wait
ai_device_query
```

Input tensor handles require read rights; output tensor handles require write rights. Work objects are returned as opaque wait-capable handles; a successful terminal wait (`DONE`/`FAILED`) releases the caller's work reference so long-lived processes do not exhaust the handle table. Device discovery returns fixed-size ABI records.

Primary files: `kernel/syscall.c`, `include/nexora/backend.h`.

## Step 9 — Capability delegation and cleanup

Implemented `ai_cap_delegate` as rights-attenuating cross-process handle duplication. A caller may delegate only rights already present on the source handle and only when the source itself has delegate permission. A backend retain hook is mandatory for delegation so shared-object lifetime cannot be duplicated without refcounting. Process cleanup releases outstanding tensor/work references and unregisters the process PID.

Primary files: `kernel/syscall.c`, `kernel/handle.c`.

## Step 10 — Userspace library, demo, and verification

Added a freestanding `libnexora` syscall wrapper layer, a demo userspace ELF, a host mock backend, syscall/handle/ELF conformance tests, a roadmap/header/docs/dispatcher cross-check, and freestanding compile checks.

Primary files: `user/libnexora/*`, `user/demo/*`, `tests/*`, `Makefile`.

Validation target:

```bash
make check
```
