# Step 5 — Canonical Kernel Virtual-Memory Layout

## Objective
Replace bootloader-owned temporary mappings with a Nexora-owned permanent kernel address space.

## Proposed 48-bit x86-64 layout
- Direct map: `0xFFFF800000000000 .. 0xFFFFC00000000000`
- Kernel heap: `0xFFFFC00000000000 .. 0xFFFFD00000000000`
- Vmalloc: `0xFFFFD00000000000 .. 0xFFFFD80000000000`
- MMIO: `0xFFFFD80000000000 .. 0xFFFFE00000000000`
- Accelerator region: `0xFFFFE00000000000 .. 0xFFFFE80000000000`
- Temporary mappings: `0xFFFFE80000000000 .. 0xFFFFF00000000000`
- Kernel image minimum: `0xFFFFFFFF80000000`

These are x86-64 port choices, not universal Nexora constants.

## Kernel image protection
`.text` R-X; `.rodata` R--; `.data/.bss` RW-; all supervisor-only; writable data NX.

## Direct map
Map appropriate RAM using firmware/boot memory types; keep physical holes/MMIO distinct. Prefer 2 MiB pages plus 4 KiB edges initially. Explicit `dmap_phys_to_virt` helpers replace ad hoc pointer arithmetic.

## Reserved windows
Heap/vmalloc reserve VA space without mapping all of it. MMIO uses explicit `ioremap` semantics. Accelerator memory gets a dedicated future-facing region. Temporary mapping slots are reserved for short-lived physical access.

## Boot transition
The new root must map current RIP, stack, GDT, IDT, TSS/IST, page tables, CPU-local state, and early console/device mappings before CR3 is switched.

## Security/readiness
Enable NX, keep KASLR-ready runtime layout metadata, leave broad identity maps behind after boot, and audit USER bits in the kernel half.
