pub mod apic;
pub mod interrupts;
pub mod memory;
pub mod task;
pub mod timer;

pub fn early_init() {
    // TODO:
    // - CPUID discovery
    // - GDT/TSS
    // - early IDT
}
