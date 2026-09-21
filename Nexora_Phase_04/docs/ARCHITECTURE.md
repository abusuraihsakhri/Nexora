# AIKernel v0 Architecture

## Current boot path

```text
GRUB / Multiboot2
        |
        v
32-bit protected-mode entry
        |
        v
minimal page tables
        |
        v
x86-64 long mode
        |
        v
kmain()
        |
        +--> console
        +--> early allocator
        +--> tensor registry
        +--> work-domain registry
        +--> rights-bearing handle tables
        +--> shared tensor backing / mapping tables
        +--> work graph
        +--> scheduler
        +--> capability prototype
```

## Proposed long-term architecture

```text
+------------------------------------------------------+
| Applications / agents / inference services          |
+--------------------------+---------------------------+
                           |
                           v
+------------------------------------------------------+
| AI Runtime ABI                                       |
| work submission | tensor handles | capabilities     |
+--------------------------+---------------------------+
                           |
                           v
+------------------------------------------------------+
| Work Graph Manager                                  |
| dependencies | deadlines | priorities | provenance  |
+--------------------------+---------------------------+
                           |
             +-------------+-------------+
             |                           |
             v                           v
+------------------------+    +------------------------+
| AI Scheduler           |    | Tensor Memory Manager  |
| CPU/GPU/NPU placement  |    | HBM/RAM/NVMe/remote   |
+-----------+------------+    +-----------+------------+
            |                             |
            +-------------+---------------+
                          |
                          v
+------------------------------------------------------+
| Capability + isolation layer                        |
+--------------------------+---------------------------+
                           |
                           v
+------------------------------------------------------+
| Device/resource model                               |
| CPU | GPU | NPU | NIC | NVMe | remote accelerator |
+------------------------------------------------------+
```

## Core kernel objects

### Tensor

A tensor is metadata describing a typed multidimensional data object. Phase 4 Step 6 gives tensors an optional first-class `ai_tensor_backing` plus backing offset; backing pages are separate from tensor metadata and can be mapped by multiple domains without copying. Phase 4 Step 7 adds managed tensor reference counting and a terminal `LIVE -> RETIRED` lifetime state. Creator pointers, handles, and mappings are now explicit tensor references; an attached tensor also retains its backing.

Important future fields:

- hardware-installed virtual mappings
- ownership
- lifetime class
- reuse distance hint
- producer/consumer relationships
- NUMA/device locality
- compression/quantization state

### Work node

Represents schedulable computation.

Important fields:

- operation type
- dependencies
- input/output tensors
- deadline
- priority
- accelerator requirements
- estimated FLOPs
- estimated memory traffic
- placement cost

### Work domain and handle

A work domain is the current kernel ownership/isolation unit. Externally visible
object references are represented by opaque 64-bit handles scoped to exactly
one domain. Handle-table entries hold object type, generation, and an explicit
rights mask (`READ`, `WRITE`, `MAP`, `SHARE`, `TRANSFER`, `ADMIN`). Rights live
in protected kernel state rather than in the numeric token and can only be
attenuated, never amplified. Phase 4 Step 5 adds kernel-mediated cross-domain `SHARE` and `TRANSFER`: the recipient receives a new domain-local token to the same object, requested rights must be a subset of the source rights, and a successful transfer invalidates the source token. Phase 4 Step 6 adds a separate generation-protected per-domain mapping namespace. `MAP` plus `READ` is required for a read mapping and `WRITE` is additionally required for writable mappings. Different domains receive different logical virtual addresses that translate to the same shared backing pages. The current prototype records this mapping/translation model but does not yet install per-domain hardware page tables. Phase 4 Step 7 makes each managed tensor/backing handle retain a reference and makes each mapping retain both tensor metadata and backing storage. Final-reference release retires objects and removes them from live registries. Because the current bump allocator has no `free()`, this is logical reclamation; physical page recycling follows the page-frame/kernel-heap work. Phase 4 Step 8 adds spinlock-protected handle/mapping namespaces, atomic tensor/backing final-reference transitions, local handle revocation, reference-pinned handle acquisition, and reference-pinned mapping views. Concurrent users must use the pinned APIs when state may be closed/revoked/unmapped by another CPU.

### Capability

Describes permission to operate on resources.

Future direction:

```text
capability = subject + object + rights + constraints
```

Example constraints:

- tensor range
- model identifier
- time limit
- accelerator quota
- network destination

## What v0 deliberately omits

- interrupts/IDT
- APIC
- SMP startup
- physical page allocator
- user mode
- syscalls
- filesystem
- networking
- PCI enumeration
- real GPU driver
- model runtime
- hardware per-domain page-table installation for tensor mappings

Those should be added only after the architectural experiments are specified.


## Phase 4 Step 9 security hardening

Opaque handle and mapping generations are terminal rather than wrapping: when a
24-bit generation reaches `0xffffff`, freeing that slot sets generation zero and
permanently removes the slot from future allocation. This prevents an ancient
stale token from becoming valid again through generation ABA.

Per-domain mapping-window arithmetic is checked before calculating
`VA_BASE + (domain_id - 1) * DOMAIN_STRIDE`; an unrepresentable window fails
closed instead of wrapping into another domain's virtual range.

The Phase 4 metadata/lifetime path is exercised by deterministic token fuzzing,
rights matrices, exhaustion tests, sanitizer runs, race matrices, and randomized
concurrent host stress before the benchmark/integration gate.


## Phase 4 completion boundary

Phase 4 now provides the complete prototype control plane for shared tensor
backing: domains, opaque rights-bearing handles, delegation, logical mappings,
reference-counted lifetime, local revocation, and concurrency hardening. The
Step 10 host integration gate also verifies that producer/consumer work nodes
can be scheduled over the same shared tensor object.

The zero-copy invariant is established at the backing layer: recipients map the
same backing object and no tensor payload copy is performed by SHARE/MAP.
Hardware address-space enforcement is intentionally deferred. Until Phase 5
installs real per-domain/user page tables, the current mapping virtual addresses
are logical kernel metadata and must not be described as MMU-enforced process
isolation.
