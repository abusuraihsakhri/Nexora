# Nexora Phase 1 Acceptance Checklist

## Bootstrap / CPU
- [ ] kernel entry reached
- [ ] early serial works
- [ ] panic without heap works
- [ ] CPUID feature discovery works
- [ ] GDT/TSS loaded

## PMM
- [ ] normalized memory map
- [ ] reserved regions excluded
- [ ] unique frame allocation
- [ ] exhaustion safe
- [ ] contiguous allocation
- [ ] stress count restores

## VMM
- [ ] active CR3 discovered
- [ ] map/unmap/translate
- [ ] W^X
- [ ] NX
- [ ] 2 MiB pages
- [ ] TLB invalidation
- [ ] stress test

## Heap
- [ ] split/coalesce
- [ ] alignment
- [ ] growth
- [ ] debug validation
- [ ] Box/Vec/String
- [ ] randomized stress

## Interrupts/time
- [ ] early/full IDT
- [ ] page-fault decode
- [ ] double-fault IST
- [ ] Local APIC
- [ ] monotonic time
- [ ] one-shot timer

## Scheduler
- [ ] kernel threads
- [ ] guarded stacks
- [ ] voluntary switching
- [ ] timer preemption
- [ ] sleep/wakeup
- [ ] block/wake
- [ ] exit/reclamation
- [ ] 100k+ context-switch stress

## Processes/syscalls
- [ ] independent user address spaces
- [ ] Ring 3 via IRETQ
- [ ] SYSCALL entry
- [ ] safe trusted kernel stack switch
- [ ] user pointer validation
- [ ] minimal ELF64 loader
- [ ] write/yield/time/exit
- [ ] user faults isolated
- [ ] process memory reclaimed

## Integration/hardening
- [ ] machine-readable self-tests
- [ ] QEMU CI boot
- [ ] syscall fuzzing
- [ ] malformed ELF rejection
- [ ] CR0.WP
- [ ] SMEP when available
- [ ] SMAP when available
- [ ] resource counters return to baseline
- [ ] reproducible build documented
