# Step 5 — Virtual Memory and x86-64 Page Tables

## Objective

Connect PMM frames to controlled virtual address spaces.

## x86-64 four-level paging

```text
PML4  9 bits
PDPT  9 bits
PD    9 bits
PT    9 bits
offset 12 bits
```

Each table is one 4-KiB physical frame containing 512 64-bit entries.

## Strong address types

Use distinct `PhysAddr` and `VirtAddr` types. Do not pass raw `u64` everywhere.

## Page sizes

Support:

- 4 KiB first;
- 2 MiB huge-page creation;
- recognize 1 GiB huge pages during translation.

## Permissions

Normal kernel mappings should enforce W^X:

```text
text    R-X
rodata  R--
data    RW-
heap    RW- NX
stacks  RW- NX
```

## Higher-half direct map

Maintain an explicit direct physical mapping:

```text
virtual = DIRECT_MAP_BASE + physical
```

Only for physical ranges intentionally present in that mapping.

MMIO is not ordinary RAM.

## VMM API

```rust
pub trait VirtualMemoryManager {
    fn map_4k(
        &mut self,
        page: Page,
        frame: PhysFrame,
        flags: MapFlags,
    ) -> Result<(), MapError>;

    fn unmap_4k(
        &mut self,
        page: Page,
    ) -> Result<PhysFrame, UnmapError>;

    fn translate(
        &self,
        addr: VirtAddr,
    ) -> Option<PhysAddr>;
}
```

## Mapping rules

When an intermediate table is missing:

1. allocate PMM frame;
2. access through direct map;
3. zero all 4096 bytes;
4. install parent entry;
5. continue traversal.

After mapping changes, invalidate relevant TLB entries.

## Important

Do not immediately replace bootloader page tables. First wrap and validate the active CR3 hierarchy. Build a clean Nexora-owned root later.

## Acceptance tests

- active CR3 discovered;
- existing hierarchy traversed;
- map/unmap/translate works;
- RWX rejected;
- 2-MiB alignment enforced;
- TLB invalidation verified;
- 4096-page stress test passes;
- PMM counts return to baseline.
