# Step 3 — Mapping, Unmapping & Protection API

## Objective
Expose generic `map`, `unmap`, `protect`, `query`, and `translate` operations over Step 2.

## Validation
Before changing page tables validate canonicality, physical-address support, alignment, page size, permissions, W^X, overflow, address-space limits, and existing mappings.

## Semantics
- Mapping over an existing leaf is rejected by default.
- Range mapping is transactional; partial success is rolled back.
- Unmapping returns old mapping metadata but does not automatically free its data frame.
- Empty PT/PD/PDPT tables can be reclaimed.
- Protection changes modify permissions without changing physical backing.
- 4 KiB leaves live at PT, 2 MiB at PD, 1 GiB at PDPT.
- Mapping a 4 KiB page inside an existing huge mapping yields a page-size conflict until splitting exists.
- Device mappings carry explicit cache/memory-type semantics.
- Guard ranges remain logically reserved with no present PTE.

## TLB contract
Page-table mutation records required invalidation; generic VM does not contain raw x86 invalidation assembly.

## Required tests
Single/adjacent maps, double-map rejection, unmap, range rollback, permission changes, RWX rejection, huge mappings/conflicts, table reclamation, device flags, query/translate.
