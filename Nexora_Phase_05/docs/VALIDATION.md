# Phase-5 Validation

## Automated checks

Run:

```bash
make check
```

The target performs three classes of checks.

### 1. Cross-file and host conformance tests

`make check` first runs `scripts/cross_check_phase5.py`, which compares the original Milestone-5 roadmap syscall list with the public ABI header, userspace wrapper declarations, dispatcher, ABI documentation, and declared host-test count. It then compiles `reference/compat_check.c` against preserved starter headers to verify the original dtype/location/tensor-flag/device/op/state numeric values and the eight-input limit. The original starter's four-output limit versus the Phase-5 ABI's eight-output limit is deliberately documented as a Phase-4 integration item rather than silently assumed compatible.

`tests/test_syscall.c` then validates 19 groups covering:

1. ABI query/version reporting.
2. ABI query before backend installation.
3. Fail-closed user access when no page-table validator is installed.
4. Tensor create/map/release lifecycle and stale-handle rejection.
5. Reserved handle-bit rejection.
6. Tensor-map output-pointer prevalidation before backend side effects.
7. Read-only rights enforcement.
8. Work submit/wait and terminal work-handle reclamation.
9. Work-submit output-fault rollback.
10. Wait timeout propagation while preserving the work handle.
11. Capability rights attenuation.
12. Delegation output-fault handling without leaked retained references.
13. Device query.
14. Invalid user-pointer rejection.
15. Unknown syscall rejection.
16. Wrong-handle-type rejection.
17. Process cleanup of delegated references plus PID unregister/reuse.
18. Valid ELF64 segment loading and executable-entry validation.
19. W^X ELF rejection.

### 2. Freestanding kernel compile check

The GDT/TSS, syscall MSR setup, syscall assembly entry, user-mode transition, handle/process/uaccess/backend/ELF/syscall C sources are compiled with:

```text
-ffreestanding -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone
```

The resulting Phase-5 kernel objects have no libc `memcpy`/`memset` dependency.

### 3. Userspace build check

A freestanding `libnexora` and `demo.elf` are linked at `0x40000000`. The resulting ELF contains separate RX, R, and RW load segments and no W+X segment.

## Additional audit-only checks

The cross-checked release was also run through Clang static analysis and a host build using AddressSanitizer plus UndefinedBehaviorSanitizer. Their captured outputs are in `artifacts/audit/`. These are release-audit checks rather than mandatory `make check` dependencies.

## Integration tests still required in the full Phase-4 kernel

Host tests cannot validate privilege transitions or page tables. The final Phase-5 integration must therefore verify in QEMU:

- CPL3 entry succeeds,
- `SYSCALL` reaches `nexora_syscall_entry`,
- syscall return restores CPL3 correctly,
- invalid user pointers are rejected without kernel panic,
- read-only tensor maps are not writable,
- two processes can map the same tensor backing without copying,
- capability delegation cannot amplify rights,
- process exit drops object references,
- page faults in userspace do not corrupt kernel state.
