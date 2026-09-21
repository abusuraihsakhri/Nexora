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

A tensor is metadata describing a typed multidimensional data object.

Important future fields:

- physical backing
- virtual mappings
- ownership
- refcount
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
- tensor backing memory

Those should be added only after the architectural experiments are specified.
