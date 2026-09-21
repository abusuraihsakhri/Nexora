# Phase 4 — Step 6: Zero-copy tensor mapping layer

## Goal

Make the Step 5 cross-domain tensor capability usable as a shared-memory data
path. Two work domains can now hold different local handles and different
logical virtual addresses while both mappings resolve to the same tensor
backing pages.

This step deliberately separates three layers:

```text
handle authority
      |
      v
 tensor metadata
      |
      v
physical backing  <---- shared by multiple mappings
      ^
      |
producer mapping     consumer mapping
```

No tensor payload is copied when a second domain maps an already-backed tensor.

## What was added

### 1. First-class tensor backing object

`ai_tensor_backing` records:

- backing ID
- backing kind
- logical byte size
- page-rounded allocation size
- page count
- kernel-visible base
- physical-base token
- live mapping count

Step 6 implements RAM backing using page-aligned storage from the current early
allocator. Because the current kernel boot path identity-maps its low memory,
the kernel build can use that address as the provisional physical base.

The backing object is intentionally separate from `ai_tensor` metadata. A
tensor now stores only a pointer to backing plus an offset into that backing.

### 2. Per-domain mapping table

Each `ai_domain` contains a generation-protected mapping table independent of
its handle table.

Mapping IDs use the same broad safety pattern as handles:

```text
63                              32 31             8 7        0
+----------------------------------+----------------+----------+
|          domain tag (32)         | generation(24) | slot(8)  |
+----------------------------------+----------------+----------+
```

A mapping ID from one domain is therefore invalid in another domain, even when
both domains map the same tensor backing.

### 3. Logical virtual-address windows

Every domain receives a disjoint prototype mapping window. Mapping creation
assigns a page-aligned logical virtual address from that window and records the
translation to the backing physical pages.

The current prototype intentionally does **not** install these mappings into
separate hardware page tables. The kernel does not yet have per-domain CR3
address spaces, a page-frame allocator, or user mode. The mapping layer is the
kernel object model and translation contract that those later mechanisms will
bind to.

This avoids pretending that Step 6 already provides hardware-enforced process
isolation.

### 4. Mapping rights

A mapping requires `MAP` plus `READ` on the tensor handle.

A writable mapping additionally requires `WRITE`.

Examples:

```text
handle rights: READ | MAP
map READ                 -> allowed
map READ | WRITE         -> rejected

handle rights: READ | WRITE | MAP
map READ | WRITE         -> allowed
```

The recipient of a Step 5 shared handle therefore cannot create a mapping that
exceeds delegated authority.

### 5. Range mapping

`ai_tensor_map_range()` supports page-aligned tensor offsets. A mapping records:

- tensor offset
- logical length
- page-rounded mapped length
- domain-local virtual base
- physical base
- kernel-visible base
- read/write protection

Sub-page starting offsets are rejected for now because the future MMU path will
operate on page-granular mappings.

### 6. Independent mapping lifetime

Handle lifetime and mapping lifetime are deliberately different.

Closing a handle does not implicitly tear down an existing mapping. A domain
cannot be destroyed while its mapping table is non-empty. Existing mappings may
be drained while the domain is `QUIESCING`.

Backing objects count live mappings independently:

```text
backing.mapping_count
```

Full object/backing reference counting and reclamation remains Step 7.

## Zero-copy acceptance path

The Step 6 host test performs the following sequence:

```text
Domain A creates tensor metadata
        |
        v
allocate two page RAM backing
        |
        v
attach backing to tensor
        |
        v
A installs READ|WRITE|MAP|SHARE handle
        |
        v
A shares READ|MAP handle to B
        |
        +---- A maps READ|WRITE
        |
        +---- B maps READ
```

Expected properties:

- A and B receive different domain-local virtual addresses.
- Both mappings resolve to the same physical base.
- Both mappings expose the same kernel backing bytes.
- A write is immediately visible through B's mapping without `memcpy`.
- B cannot create a writable mapping.
- A mapping ID is invalid in the wrong domain.
- Mapping generation protects stale mapping IDs.
- A domain cannot be destroyed while mappings remain live.
- Backing mapping count returns to zero after unmapping.

## APIs

Primary Step 6 APIs:

```c
ai_tensor_backing *ai_backing_create_ram(u64 size_bytes, u32 flags);

bool ai_tensor_attach_backing(
    ai_tensor *tensor,
    ai_tensor_backing *backing,
    u64 backing_offset
);

ai_mapping_id_t ai_tensor_map(
    ai_domain *domain,
    ai_handle_t tensor_handle,
    ai_mapping_prot_t protection
);

ai_mapping_id_t ai_tensor_map_range(
    ai_domain *domain,
    ai_handle_t tensor_handle,
    u64 tensor_offset,
    u64 length,
    ai_mapping_prot_t protection
);

bool ai_tensor_unmap(ai_domain *domain, ai_mapping_id_t mapping);
```

Diagnostic/translation APIs expose the mapping's logical virtual address,
physical address, kernel address, and VA-to-physical translation.

## Concurrency boundary

Step 6 still assumes the single-core prototype execution model. Mapping-table
mutation, backing mapping counts, handle delegation, and unmap operations are
not yet protected by locks or atomics. Phase 4 Step 8 will address revocation,
stale-handle/mapping concurrency, and SMP-safe synchronization.

## Next step

Phase 4 Step 7: reference counting and lifetime management.

The next step will make handles, tensors, backings, and mappings participate in
an explicit ownership graph so that backing pages survive exactly as long as a
legitimate reference or mapping remains and can then be reclaimed safely.
