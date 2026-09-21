# Nexora Phase 5 — User Mode + AI-Native Syscall ABI

Phase 5 introduces the first **userspace execution boundary** for Nexora and exposes the AI object model through a deliberately narrow syscall ABI rather than cloning POSIX.

This directory is a **Phase-5 integration package** intended to be overlaid onto the completed Phase-4 tree. It contains the new Phase-5 implementation, a host-side conformance harness, x86-64 ring-3/syscall primitives, an ELF64 userspace loader, and a small freestanding userspace library/demo.

## Phase-5 ABI

The implemented ABI is:

```text
nexora_abi_query
ai_tensor_create
ai_tensor_map
ai_tensor_release
ai_work_submit
ai_work_wait
ai_cap_delegate
ai_device_query
```

The ABI uses 64-bit opaque handles with:

- type checking,
- generation counters for stale-handle rejection,
- per-handle rights,
- rights attenuation during delegation.

## What is implemented

- versioned ABI structures in `include/nexora/abi.h`
- per-process handle tables and PID registry
- user-pointer validation/copy layer with a Phase-4 VM validation hook
- strict syscall dispatcher and negative errno-style return values
- tensor create/map/release syscalls
- work submit/wait syscalls
- device enumeration syscall
- cross-process capability/handle delegation with rights attenuation
- process-exit resource cleanup path
- x86-64 GDT with ring-0/ring-3 descriptors and TSS
- x86-64 `SYSCALL/SYSRET` MSR setup
- x86-64 syscall entry assembly
- `IRETQ` ring-3 entry primitive
- ELF64 loader with bounds checks and W^X enforcement
- freestanding userspace syscall wrapper library
- freestanding Phase-5 demo ELF
- 19 host-side test groups
- freestanding architecture compile checks

## Directory layout

```text
include/nexora/
  abi.h              public ABI
  backend.h          bridge to Phase-4 tensor/work/device implementation
  elf64.h            minimal userspace ELF loader contract
  handle.h           typed capability handles
  process.h          process object
  syscall.h          dispatcher/frame interface
  uaccess.h          safe user-copy contract
  x86_64.h           GDT/TSS/syscall/usermode interface

kernel/
  backend.c
  elf64.c
  handle.c
  process.c
  syscall.c
  uaccess.c

arch/x86_64/
  gdt.c
  syscall_entry.S
  syscall_msr.c
  usermode.S

user/
  libnexora/         userspace wrappers
  demo/              minimal userspace Phase-5 smoke program
  user.ld

tests/
  mock_backend.c
  test_syscall.c

docs/
  PHASE5_STEPS.md
  ABI.md
  INTEGRATION.md
  SECURITY.md
  VALIDATION.md
  CROSS_CHECK_REPORT.md

reference/
  ORIGINAL_ROADMAP.md  source roadmap used by the consistency checker
```

## Validation

Run:

```bash
make check
```

This first checks roadmap/header/docs/wrapper/dispatcher consistency, then executes the host-side syscall/handle/ELF tests, compiles the x86-64 kernel pieces as freestanding objects, and builds the userspace demo ELF.

Expected result:

```text
Phase 5 cross-file consistency: PASS
Phase 5 host tests: PASS (19 test groups)
Phase 5 validation complete.
```

## Cross-file audit conclusion

The original AIKernel roadmap, public ABI header, userspace wrapper declarations, syscall dispatcher, ABI documentation, and tests were cross-checked against one another. The seven Milestone-5 AI syscalls match exactly across those sources; `nexora_abi_query` is the additional ABI-discovery call.

The audit also corrected lifecycle and boundary issues found during review: fail-closed user-pointer validation, rollback/prevalidation around side-effecting syscalls, process unregister on teardown, terminal work-handle reclamation, stricter ELF entry validation, reserved-handle-bit rejection, and syscall-entry register/stack ABI alignment.

**Conclusion:** the available files reach the same conclusion: Phase 5 is internally coherent as a code-level integration overlay, but it is **not yet full-kernel integration proof**. Final acceptance still requires the real completed Phase-4 tree and QEMU/hardware validation of CPL3 entry, page-table enforcement, `SYSCALL/SYSRET`, and true zero-copy tensor mapping. See `docs/CROSS_CHECK_REPORT.md`.

## Integration boundary with Phase 4

Phase 5 does **not** duplicate the Phase-4 tensor allocator, graph scheduler, page-frame allocator, or zero-copy tensor-sharing implementation. Instead, `struct nexora_backend_ops` is the adapter boundary. Wire those operations to the actual Phase-4 implementation.

The other required integration point is `nexora_uaccess_set_validator()`: connect it to the Phase-4 page-table walker so copy-in/copy-out verifies user mappings and permissions before touching memory. User access is fail-closed: nonzero-length user copies are rejected until a validator is installed.

See `docs/INTEGRATION.md` for the exact boot and launch sequence.

## Important SMP note

The provided syscall entry path intentionally uses a single bootstrap kernel-stack pointer. That is correct for the Phase-5 bring-up target, but it is **not SMP-safe**. Before running user tasks concurrently on multiple CPUs, replace the global syscall stack state with per-CPU storage (normally GS-based) and add the corresponding interrupt/NMI hardening.

## Exit criteria

Phase 5 is considered integrated when a Phase-4 kernel can:

1. create an isolated process/address space,
2. load `build/user/demo.elf` into user mappings,
3. enter CPL3,
4. execute `SYSCALL`,
5. create/map a tensor without copying its backing,
6. submit and wait for work,
7. query a device,
8. delegate a restricted tensor handle to another process,
9. reject stale/wrong-type/overprivileged handles,
10. reclaim process-owned handles at process teardown.

That is the boundary required before Phase 6, the host-side Linux comparison harness.
