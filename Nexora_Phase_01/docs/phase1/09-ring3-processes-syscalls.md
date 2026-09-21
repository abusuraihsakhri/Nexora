# Step 9 — Ring 3, Processes, Syscalls, and First Userspace

## Objective

Create a hardware-enforced user/kernel protection boundary.

## Process vs thread

A thread represents execution.  
A process represents an address space, resources, and an isolation boundary.

Kernel threads may have no owning user process.

## Address spaces

Every user process gets an independent page-table root.

The upper kernel half can be shared but remains supervisor-only.

The lower half is process-specific.

## User layout

Keep page zero unmapped.

Map:

```text
.text    R-X
.rodata  R--
.data    RW-
.bss     RW-
stack    RW- NX
```

Enforce W^X.

Every physical page exposed to a user process must be zeroed first.

## User and kernel stacks

A user thread owns:

- one Ring-3 user stack;
- one Ring-0 kernel stack.

TSS.RSP0 must point to the currently scheduled user thread's kernel stack.

## First Ring-3 transition

Use `IRETQ` with validated user selectors, RIP, RSP, and RFLAGS.

## Native syscall ABI v0

Suggested register convention:

```text
RAX syscall number
RDI arg1
RSI arg2
RDX arg3
R10 arg4
R8  arg5
R9  arg6
RAX return value
```

Initial syscall set:

```text
exit
write
yield
get_time
```

Use `SYSCALL/SYSRET` for ordinary calls. Initial launch still uses IRETQ.

## Important syscall-entry property

`SYSCALL` does not automatically switch to the TSS kernel stack. The entry stub must switch to a trusted kernel stack immediately, typically using per-CPU state.

## User pointers

Never treat user addresses as trusted kernel pointers.

Provide explicit:

```text
validate_user_range
copy_from_user
copy_to_user
```

with canonical-range, overflow, mapping, and permission checks.

## ELF64 loader

Initially support only what is required:

- ELF64;
- little endian;
- x86-64;
- program headers;
- `PT_LOAD`;
- entry point;
- BSS zeroing;
- final page permissions.

No dynamic linker/shared libraries are required.

Embed the first `init.elf` into the kernel to avoid introducing a filesystem dependency.

## Fault policy

User faults:

```text
terminate process
```

Kernel faults:

```text
panic
```

A broken process must not crash Nexora.

## Acceptance tests

- independent per-process mappings;
- kernel memory inaccessible to Ring 3;
- privileged instruction faults;
- page-zero fault;
- write-to-text fault;
- invalid syscall safe;
- invalid user pointer safe;
- user-mode preemption;
- process crash isolation;
- process teardown returns physical/page-table frames.
