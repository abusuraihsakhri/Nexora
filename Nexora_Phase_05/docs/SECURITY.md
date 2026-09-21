# Phase-5 Security Review

## Security properties implemented

### Typed handles

A tensor syscall cannot consume a work handle. The slot type and encoded type must both match.

### Stale-handle and reserved-bit rejection

Every slot carries a generation counter. Closing a handle increments the generation, so a later object occupying the same slot does not validate against the old handle value. Reserved high handle bits must be zero, which rejects noncanonical forged variants of an otherwise valid handle.

### Capability attenuation

Delegation cannot add rights. The delegated rights mask must be a subset of the source rights, and the source must explicitly permit delegation.

### User-pointer validation

All structured syscall inputs and outputs pass through copy-in/copy-out helpers. Arithmetic wraparound and process VA bounds are checked, and user access fails closed until the Phase-4 page-table validator is installed. Side-effecting calls prevalidate writable output buffers before invoking the backend where practical.

### ELF loader validation

The loader validates ELF class, endianness, machine, type, header bounds, program-header bounds, segment file/memory sizes, user VA ranges, and rejects W+X load segments.

### No physical addresses in the ABI

Userspace receives opaque handles and user virtual addresses only.

### Reference-lifetime enforcement

Delegation is disabled unless the backend supplies a retain hook. Tensor/work creation paths require matching release hooks, terminal work waits reclaim their process-local reference, and process teardown releases remaining references before unregistering the PID.

## Required Phase-4 enforcement

The following must be provided by the integrated Phase-4 VM/object layer:

- page-table-level user-access validation,
- page ownership/isolation,
- true tensor object reference counting,
- zero-copy map permission enforcement,
- prevention of writable aliases to read-only tensor handles,
- process-specific address spaces,
- scheduler-safe object lifetime.

## Known Phase-5 bootstrap limitations

### Single global syscall stack

`syscall_entry.S` currently uses one global kernel-stack pointer and one saved user RSP. This must become per-CPU before concurrent SMP userspace.

### No hardened `SYSRET` canonical-address recovery path

A production kernel must validate user RIP/RSP and handle noncanonical return state without allowing a fault at an unsafe privilege transition point. The current path assumes the kernel itself created the user context and that return state remains canonical.

### No SMAP/SMEP enablement in this package

Phase 5 creates the software boundary but does not enable CR4 SMEP/SMAP. Once the Phase-4 mappings and exception path are mature, enable them and use explicit `STAC/CLAC` around copy-in/copy-out where required.

### No signal/exception ABI

Userspace page faults and exceptions need a defined termination/recovery policy. Do not expose general-purpose signals merely to imitate POSIX; define only mechanisms required by Nexora workloads.

### Handle generation is not a cryptographic secret

The handle table protects against stale references and type confusion, not against a compromised kernel or arbitrary kernel memory disclosure. Authorization remains kernel-enforced through the per-process table and rights mask.

### Generation-wrap limit

Handle generations are currently 16-bit. They prevent ordinary stale-handle reuse, but a slot recycled through a full generation wrap could theoretically recreate an old opaque value. Before hostile multi-tenant workloads, widen the generation epoch or add an equivalent anti-ABA mechanism.

### User-mapping TOCTOU

The Phase-5 uaccess layer calls the Phase-4 validator and then performs the byte copy. If another CPU can mutate the same process page tables concurrently, the integrated VM must hold an appropriate mapping lock, pin the range, or provide recoverable fault handling across validation plus copy.

### Partial ELF mapping on backend failure

The loader completes all structural validation before it begins mapping. If the Phase-4 `map_segment` callback itself fails after earlier segments were mapped, the current interface does not roll those mappings back. The launch path should discard the new address space/process on loader failure, or Phase 4 should provide a transactional mapping wrapper.

## Recommended hardening before Phase 7 real-device work

- per-CPU syscall entry state,
- canonical return validation,
- SMEP/SMAP,
- guard pages for user and kernel stacks,
- kernel stack randomization or separation per task,
- explicit process teardown hooks in scheduler paths,
- fuzzing of every syscall descriptor and ELF parser,
- formalized handle-rights invariants,
- audit of DMA mappings so devices cannot bypass process isolation.
