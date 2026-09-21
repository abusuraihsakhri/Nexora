# Step 3 — x86-64 CPU Foundation

## Objective

Establish the CPU state required before memory protection, exceptions, and scheduling become dependable.

## Components

- CPUID feature discovery;
- model-specific-register helpers;
- GDT;
- TSS skeleton;
- segment selectors;
- control-register helpers;
- canonical-address checks;
- CPU feature record.

## Required feature discovery

Record at minimum:

- long mode;
- NX;
- APIC;
- x2APIC;
- TSC;
- invariant TSC;
- TSC-deadline mode;
- SMEP;
- SMAP;
- XSAVE;
- PCID when available.

## GDT

Prepare descriptors for:

```text
kernel code  DPL0
kernel data  DPL0
user code    DPL3   (used in Step 9)
user data    DPL3   (used in Step 9)
TSS
```

Do not depend on hardware task switching.

## Address validation

For standard 48-bit virtual addressing, reject noncanonical virtual addresses.

## Control-register policy

Later phases will require:

- `CR0.WP = 1`;
- NX enabled through EFER;
- optional SMEP/SMAP when supported;
- CR3 helpers isolated in the architecture layer.

## Acceptance criteria

- CPUID feature record is generated.
- GDT can be loaded.
- TSS descriptor is valid.
- CPU helper code is isolated from generic kernel code.
- Unsupported mandatory features cause a clear boot failure.
