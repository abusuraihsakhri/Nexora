# Phase 2 Public API Summary

The rest of Nexora should use VM abstractions rather than page-table internals.

## Core types
`phys_addr_t`, `virt_addr_t`, `phys_frame_t`, `virt_page_t`, `page_size_t`, `vm_permissions_t`, `vm_flags_t`, `memory_intent_t`, `vm_result_t`.

## Address spaces
```c
vm_result_t vm_space_create(vm_space_t **out);
void vm_space_get(vm_space_t *space);
void vm_space_put(vm_space_t *space);
vm_result_t vm_space_activate(vm_space_t *space);
vm_space_t *vm_space_current(void);
vm_result_t vm_space_clone_cow(vm_space_t *parent, vm_space_t **child);
```

## Regions
```c
vm_result_t vm_region_create(...);
vm_result_t vm_region_allocate(...);
vm_result_t vm_region_remove(...);
vm_result_t vm_region_protect(...);
vm_region_t *vm_region_find(vm_space_t *space, virt_addr_t address);
bool vm_region_overlaps(...);
```

## Mapping
```c
vm_result_t vm_map_page(...);
vm_result_t vm_map_range(...);
vm_result_t vm_unmap_page(...);
vm_result_t vm_unmap_range(...);
vm_result_t vm_protect_page(...);
vm_result_t vm_protect_range(...);
vm_result_t vm_query_mapping(...);
vm_result_t vm_translate(...);
```

## Fault/diagnostics
```c
vm_fault_result_t vm_handle_fault(vm_space_t *space, const vm_fault_info_t *fault);
bool vm_space_validate(const vm_space_t *space);
void vm_space_dump(const vm_space_t *space);
void vm_regions_dump(const vm_space_t *space);
bool vm_security_audit_space(const vm_space_t *space);
```

## Architecture-private details
PML4/PDPT/PD/PT, x86 PTE encoding, CR3, INVLPG/INVPCID, PAT/cache-bit encoding, and x86 huge-page mechanics stay under `arch/x86_64`.
