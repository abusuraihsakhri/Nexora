# Proposed x86-64 Kernel Virtual Layout

```text
0xFFFF800000000000  Direct physical map
0xFFFFC00000000000  Kernel heap
0xFFFFD00000000000  Vmalloc/dynamic mappings
0xFFFFD80000000000  MMIO
0xFFFFE00000000000  Accelerator/shared-device window
0xFFFFE80000000000  Temporary mapping window
0xFFFFFFFF80000000  Kernel-image minimum / final 2 GiB window
```

Represent intervals as `[BASE, END)`. Compile-time validate canonicality, ordering, alignment, and non-overlap.
These are initial x86-64 port constants, not architecture-independent Nexora constants.
