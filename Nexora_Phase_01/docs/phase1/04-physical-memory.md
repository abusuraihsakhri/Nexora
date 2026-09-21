# Step 4 — Physical Memory Manager

## Objective

Create a trustworthy allocator for physical page frames.

## Base page size

```text
PAGE_SIZE = 4096 bytes
```

## Logical frame states

```text
UNAVAILABLE
FREE
RESERVED
ALLOCATED
```

Unknown memory is never allocatable.

## Memory-region normalization

```rust
#[repr(u8)]
pub enum MemoryRegionKind {
    Usable,
    Reserved,
    Kernel,
    Bootloader,
    Acpi,
    Mmio,
    Framebuffer,
    Unknown,
}

pub struct MemoryRegion {
    pub start: u64,
    pub length: u64,
    pub kind: MemoryRegionKind,
}
```

Usable memory must be aligned inward to complete 4-KiB frames.

## Initial allocator

Use a bitmap allocator first:

```text
0 = free
1 = unavailable / reserved / allocated
```

At 128 GiB RAM, a one-bit-per-4-KiB-frame bitmap requires about 4 MiB.

## Initialization policy

1. Mark all frames unavailable.
2. Parse normalized boot memory map.
3. Mark only explicitly usable RAM free.
4. Reserve kernel image.
5. Reserve boot structures.
6. Reserve allocator metadata.
7. Reserve page tables.
8. Reserve framebuffer.
9. Reserve ACPI/firmware/MMIO.
10. Reserve architecture-specific ranges.

## Interface

```rust
pub trait PhysicalMemoryManager {
    fn alloc_frame(&mut self) -> Option<PhysFrame>;
    fn alloc_contiguous(
        &mut self,
        count: usize,
        alignment_pages: usize,
    ) -> Option<PhysFrame>;
    fn free_frame(&mut self, frame: PhysFrame);
    fn reserve_range(&mut self, start: u64, length: u64);
    fn total_frames(&self) -> usize;
    fn free_frames(&self) -> usize;
}
```

## Invariants

- no frame allocated twice;
- reserved frame cannot be freed through ordinary API;
- unknown regions stay unavailable;
- allocator metadata cannot allocate itself;
- kernel image never enters free pool;
- accounting remains consistent.

## Acceptance tests

- allocate/free/reuse;
- 10,000+ unique allocations;
- exhaustion returns `None`;
- free count restores after stress;
- contiguous allocation and alignment tests;
- reserved-range exclusion.
