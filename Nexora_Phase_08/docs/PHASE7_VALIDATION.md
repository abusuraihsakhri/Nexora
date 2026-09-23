# Phase 7 Validation Record

## Build environment used for this package

Available:

- `gcc`
- `ld`
- `nm`

Not available in the execution environment used to prepare this package:

- `grub-file`
- `grub-mkrescue`
- `qemu-system-x86_64`
- `xorriso`

Therefore the kernel compilation/link and static symbol gate were executed, while ISO creation and runtime QEMU boot were not claimed as tested.

## Executed command

```bash
make clean
make phase7-check
```

## Result

**PASS**

The build completed with:

```text
-std=c11
-O2
-Wall
-Wextra
-Werror
-ffreestanding
-fno-stack-protector
-fno-pic
-fno-pie
-m64
-mno-red-zone
```

`build/kernel.elf` linked successfully and all required Phase 7 symbols were found. The independent cross-check also verified that the Multiboot2 header is aligned, checksum-valid, terminated correctly, and located within the first 32 KiB of the ELF image. The linked entry point remains inside the starter kernel's initial 1 GiB identity map.

## Required runtime output after QEMU boot

When QEMU/GRUB tooling is available, the expected Phase 7 indicators are:

```text
Nexora AI-native kernel booted.
Simulated accelerator registered.

[Phase 7 device discovery]
PCI devices discovered: ...

[Phase 7 DMA + simulated accelerator]
DMA command status: OK
DMA copy verification: PASS

[Nexora AI work graph]
device path result: SIMULATED
...

Nexora Phase 7 initialization complete.
```

The exact PCI list depends on QEMU machine configuration.

## Baseline boot-layout correction

During validation, the original starter assembly section did not mark `.multiboot_header` as allocatable. That allowed the linker to place the executable text at address zero while the Multiboot2 section sat separately at 1 MiB/file offset beyond the normal bootloader search window. Phase 7 corrects the assembly section flags and discards compiler unwind metadata, restoring a coherent load layout beginning at 1 MiB.

This is a baseline correctness fix discovered by the Phase 7 cross-check, not a new accelerator feature.

## Negative-safety checks

The implementation was also checked for these architectural errors:

- no vendor-specific GPU register programming;
- no claim that PCI discovery equals a functional device driver;
- no direct user-supplied raw physical address ABI;
- no silent claim that simulated work performed real matrix/attention computation;
- no cloud/runtime dependency;
- no libc dependency introduced.

## Remaining runtime tests

Run these in the normal Nexora build VM/WSL/Linux environment:

```bash
make check
make
make run
```

Then confirm:

1. Multiboot2 header verification passes.
2. ISO is created.
3. PCI enumeration completes.
4. DMA copy reports `PASS`.
5. both graph work nodes report `SIMULATED` completion.
6. accelerator counters show at least three submissions: one DMA copy plus two work nodes.
