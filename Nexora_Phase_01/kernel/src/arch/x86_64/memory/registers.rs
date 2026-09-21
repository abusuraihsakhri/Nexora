use crate::mm::address::{PhysAddr, VirtAddr};

pub unsafe fn read_cr2() -> VirtAddr {
    let value: u64;
    unsafe {
        core::arch::asm!(
            "mov {}, cr2",
            out(reg) value,
            options(nomem, nostack, preserves_flags)
        );
    }
    VirtAddr(value)
}

pub unsafe fn read_cr3() -> PhysAddr {
    let value: u64;
    unsafe {
        core::arch::asm!(
            "mov {}, cr3",
            out(reg) value,
            options(nomem, nostack, preserves_flags)
        );
    }
    PhysAddr(value & 0x000f_ffff_ffff_f000)
}
