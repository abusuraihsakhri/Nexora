use crate::{arch, mm};

#[repr(C)]
pub struct BootInfo {
    pub physical_memory_offset: u64,
    pub memory_map_ptr: u64,
    pub memory_map_len: usize,
}

pub fn kernel_main(boot_info: &'static BootInfo) -> ! {
    arch::x86_64::early_init();
    validate_boot_info(boot_info);

    // Phase 1 initialization order:
    // 1. early fault-capable CPU state
    // 2. PMM
    // 3. VMM
    // 4. heap
    // 5. full interrupts/APIC/time
    // 6. scheduler
    // 7. syscalls/processes
    // 8. userspace init
    //
    // Concrete bootloader integration is intentionally left to the
    // implementation phase.

    let _ = mm::PAGE_SIZE;

    halt_loop()
}

fn validate_boot_info(info: &BootInfo) {
    assert!(info.memory_map_len > 0, "empty boot memory map");
}

pub fn halt_loop() -> ! {
    loop {
        unsafe {
            core::arch::asm!("hlt", options(nomem, nostack));
        }
    }
}
