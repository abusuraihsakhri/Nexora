# Step 2 — x86-64 Page-Table Manager

## Objective
Build the architecture-specific four-level page-table machinery.

## Structure
`PML4 → PDPT → PD → PT → 4 KiB frame`; each table is one 4 KiB page containing 512 64-bit entries.

## Requirements
- Wrap raw PTE values and expose helpers for Present, Writable, User, Huge, Global, NX, address and flags.
- Root is stored as a physical address because CR3 refers to a physical root.
- Allocate every new page-table level from PMM and zero it before installation.
- Provide lookup-only and create-missing walking modes.
- Recognize 1 GiB/2 MiB huge leaves at PDPT/PD; never treat them as child tables.
- Preserve parent traversal permissions for user/writable mappings.
- Provide virtual→physical translation.
- Destruction frees page-table frames, not blindly the mapped data frames.
- Keep shared kernel subtrees externally owned.
- Expose CR3 read/write helpers but keep PCID-ready abstractions.
- Roll back intermediate table allocations if a walk fails.
- Provide validation and a page-table dump.

## Required tests
Empty root, missing lookup, create walk, hierarchy reuse, translation, huge-page recognition, injected OOM rollback, and safe destruction.
