# Nexora Phase 5 — Cross-File Audit Report

Audit date: 2026-09-20  
Package: `0.5.1-phase5-crosschecked`

## Scope

This audit cross-checked the available Phase-5 package against the original `aikernel-starter` roadmap and then checked the Phase-5 files against one another: public ABI definitions, dispatcher, userspace wrappers, x86-64 entry code, handle/process/uaccess code, ELF loader, tests, integration/security documentation, build artifacts, and validation logs.

A completed Phase-4 source snapshot was not available in the current working set. Therefore the Phase-4 backend cannot be verified byte-for-byte here. The audit can verify the Phase-5 overlay contract and the original roadmap, but the final Phase-4 integration remains an external acceptance step.

## Conclusion

**Yes. The available files reach the same technical conclusion.**

The original roadmap defines Milestone 5 as user mode plus the following seven AI-native calls:

```text
ai_tensor_create
ai_tensor_map
ai_tensor_release
ai_work_submit
ai_work_wait
ai_cap_delegate
ai_device_query
```

Those seven names are present consistently in the public ABI, userspace wrapper layer, syscall dispatcher, ABI documentation, and README. Phase 5 additionally exposes `nexora_abi_query` as syscall 0 for ABI discovery. This does not change the roadmap's AI operation set.

The correct status statement after cross-checking is:

> **Phase 5 is internally coherent and host-validated as an integration overlay. It is not yet proof of full-kernel Phase-4 integration.** Final acceptance requires the real completed Phase-4 tree plus QEMU/hardware validation of CPL3 entry, page-table enforcement, `SYSCALL/SYSRET`, and true shared-backing tensor mapping.

This wording is now consistent across the README, integration guide, security review, release notes, and validation document.

## Consistency matrix

| Area | Cross-check result | Notes |
|---|---|---|
| Original Milestone-5 syscall set | PASS | 7/7 roadmap calls matched exactly |
| Syscall numbering | PASS | Header and ABI documentation agree: 0–7, `NEXORA_SYS_MAX=8` |
| Userspace wrapper declarations | PASS | All public calls declared and implemented |
| Dispatcher coverage | PASS | Every numbered syscall has a dispatch path |
| x86-64 register ABI | PASS after hardening | Wrapper/frame/entry agree on `rax,rdi,rsi,rdx,r10,r8,r9`; argument registers are preserved on return |
| SysV kernel-stack call alignment | PASS after hardening | syscall bootstrap stack is aligned and call site padded correctly |
| Tensor/work enum semantics | PASS against starter concepts | dtype, location, device-mask, operation, and state values preserve the starter ordering used by the ABI |
| Handle layout | PASS | index, generation, type, and reserved-bit behavior agree with documentation |
| Capability attenuation | PASS | target rights must be a subset of source rights; retain hook required |
| User-pointer boundary | PASS after hardening | fail-closed without page-table validator; side-effect outputs are prevalidated/rolled back |
| Process teardown | PASS after hardening | outstanding references are released and PID is unregistered |
| Work-object lifecycle | PASS after hardening | terminal wait consumes caller's work reference; timeout leaves it valid |
| ELF64 loader | PASS after hardening | ET_EXEC/x86-64/bounds/W^X plus executable-entry validation |
| Freestanding build | PASS | kernel architecture/source objects compile with `-ffreestanding -Werror` |
| Userspace ELF | PASS | separate RX/R/RW load segments; no W+X load segment |
| Host test suite | PASS | 19 test groups |
| Strict warning build | PASS | `-Wpedantic -Wconversion -Wshadow -Werror` plus 19 host groups |
| Clang static analyzer | PASS | no diagnostics in audited source set |
| ASan/UBSan host suite | PASS | 19 groups pass under address/undefined-behavior sanitizers |

## Issues found and corrected during the cross-check

The first Phase-5 package was broadly consistent but the deeper audit exposed boundary conditions worth fixing before it is treated as the reference package. The cross-checked revision corrects the following:

1. User-copy validation previously allowed bounds-only access when no Phase-4 validator was installed. It now fails closed for nonzero-length accesses.
2. `ai_work_submit` could retain a backend work object if the final handle copy-to-user failed. Output memory is now prevalidated and the path has rollback.
3. `ai_tensor_map` could perform a backend mapping before discovering an invalid output pointer. The output address slot is now validated before the backend side effect.
4. Capability delegation could install/retain a target reference before a copy-back failure. Writable request memory is prevalidated and a rollback path is present.
5. Delegation previously tolerated a missing retain hook, which is unsafe for shared reference lifetime. Retain is now mandatory.
6. Work handles had no bounded lifecycle except process exit. A successful terminal `ai_work_wait` now releases and removes the caller's work reference; timeout preserves it.
7. Process cleanup released handles but did not unregister the PID. Cleanup now unregisters the process and clears the current-process pointer when applicable.
8. The ELF loader accepted an entry address anywhere in the user VA range. It now requires the entry point to lie in an executable PT_LOAD segment and performs a structural validation pass before mapping.
9. High reserved bits in handles were ignored. Nonzero reserved bits are now rejected.
10. The assembly syscall path called C with a potentially misaligned SysV stack and did not restore the argument registers even though the userspace wrapper declared only RCX/R11 as clobbered. The entry path now aligns the call and restores the argument registers.
11. ABI query previously depended on the Phase-4 backend being installed. `nexora_abi_query` can now report the ABI once a process/user validator exists even before backend installation.

## Phase-4 integration assumptions that remain to be checked

The available original starter provides the architectural direction, but it is not the completed Phase-4 source. The following must be verified against that actual tree during overlay integration:

- `tensor_map` must map the same physical tensor backing into the target process rather than copy data.
- The Phase-4 object layer must implement matching retain/release semantics for delegated tensors and work objects.
- The page-table validator must check present/user/write permissions across the complete range and coordinate safely with mapping changes.
- The scheduler/context switch must update CR3, current process, TSS RSP0, and syscall kernel stack consistently.
- The Phase-5 ABI permits up to 8 input and 8 output tensor handles per work descriptor. The original starter v0 had 8 inputs but 4 outputs; the completed Phase-4 backend must either support the ABI limit or reject unsupported counts cleanly. This is an integration compatibility item, not an internal Phase-5 inconsistency.
- A real fault/exception path must contain bad userspace accesses without corrupting kernel state.

## Validation evidence

Run:

```bash
make check
```

Expected audited result:

```text
Phase 5 cross-file consistency: PASS
Roadmap milestone calls: 7 / 7 matched
Header/docs/wrappers/dispatcher syscall numbering: matched
Host test groups declared: 19
Starter enum/value compatibility: PASS (known output-limit delta documented)
Phase 5 host tests: PASS (19 test groups)
Phase 5 validation complete.
```

Additional audit artifacts are under `artifacts/audit/`:

- `strict_compile.txt` — strict warning build and host-test result.
- `clang_analyzer.txt` — empty, indicating no analyzer diagnostics in the checked source set.
- `sanitizer.txt` — ASan/UBSan host-test result.
- `syscall_entry_objdump.txt` — disassembly of the audited syscall entry object.
- `freestanding_undefined_symbols.txt` — unresolved cross-object symbols; checked for libc memory/allocation dependencies.

## Final gate

The package can be used as the Phase-5 reference overlay. Do **not** label the integrated kernel as Phase-5-complete until the QEMU/full-kernel checks in `docs/VALIDATION.md` pass against the actual completed Phase-4 tree.
