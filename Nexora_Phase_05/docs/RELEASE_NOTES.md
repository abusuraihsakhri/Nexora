# Nexora Phase 5 Release Notes

Phase 5 establishes the first protected user/kernel interface for Nexora.

Key additions:

- CPL3 execution substrate on x86-64.
- TSS-backed ring transition support.
- `SYSCALL/SYSRET` fast syscall entry.
- Versioned AI-native userspace ABI.
- Typed generation-safe handles.
- Tensor create/map/release calls.
- Work submit/wait calls.
- Capability/handle delegation with rights attenuation.
- Device query ABI.
- ELF64 userspace loader with W^X enforcement.
- Freestanding `libnexora` wrappers and demo ELF.
- Cross-file roadmap/ABI consistency check, 19 host conformance test groups, sanitizer/static-analysis pass, and architecture compile checks.

The package is intentionally an overlay/delta for the completed Phase-4 kernel. The Phase-4 memory manager, tensor backing, scheduler, zero-copy sharing implementation, and device model remain authoritative and are connected through `nexora_backend_ops`.

The bootstrap syscall stack is single-CPU. Per-CPU syscall state is a mandatory hardening task before concurrent SMP user processes.


## Cross-check hardening revision

The cross-file audit corrected several boundary conditions without changing the original seven-call Milestone-5 AI ABI: user access now fails closed without a Phase-4 validator; side-effecting output paths prevalidate/rollback; terminal work handles are reclaimed; process cleanup unregisters PIDs; delegation requires backend retain support; the ELF loader validates the entry point against an executable segment; reserved handle bits are rejected; and the syscall entry preserves argument registers while maintaining SysV stack alignment.

The audit does **not** convert host validation into proof of full Phase-4 integration. CPL3 transitions, real page tables, true shared physical tensor backing, and `SYSCALL/SYSRET` must still be exercised in the integrated kernel.
