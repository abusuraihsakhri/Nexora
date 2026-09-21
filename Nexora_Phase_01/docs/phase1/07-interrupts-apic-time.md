# Step 7 — Exceptions, IDT, Local APIC, and Monotonic Time

## Objective

Create reliable CPU-fault handling and the timing substrate required by preemption.

## Required architecture

```text
GDT / TSS
   ↓
IDT
   ├─ exceptions
   └─ hardware interrupts
          ↓
      Local APIC
          ↓
       timer
          ↓
   monotonic clock
```

## IDT

Install 256 entries and explicitly support at least:

- divide error;
- breakpoint;
- invalid opcode;
- double fault;
- general protection fault;
- page fault;
- alignment check;
- machine check.

Use normalized assembly stubs so Rust receives one stable `TrapFrame`.

## Page faults

Read CR2 and decode:

- present/not-present;
- read/write;
- user/supervisor;
- reserved-bit violation;
- instruction fetch.

Unexpected kernel page fault is fatal during Phase 1.

## Double fault

Reserve a dedicated TSS IST stack for #DF. This prevents many stack-corruption faults from immediately triple-faulting the CPU.

## Local APIC

Abstract xAPIC/x2APIC behind a common interface. Phase 1 may implement xAPIC first while preserving the abstraction.

Mask legacy PIC interrupts once APIC delivery is established.

## Timer policy

Prefer:

```text
Invariant TSC
+
TSC-deadline Local APIC timer
```

when supported.

Fallback to calibrated Local APIC one-shot operation.

PIT may be used as a calibration fallback, not as the permanent scheduler clock.

## Monotonic API

Expose:

```rust
pub fn now() -> Instant;
```

The rest of the kernel must not depend directly on APIC timer registers or raw TSC values.

## Acceptance tests

- breakpoint handler;
- controlled #UD;
- page-fault report;
- double-fault IST;
- APIC timer interrupt;
- monotonicity;
- one-shot deadlines;
- useful fault diagnostics;
- no heap allocation in interrupt paths.
