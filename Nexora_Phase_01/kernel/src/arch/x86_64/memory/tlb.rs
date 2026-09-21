use crate::mm::address::VirtAddr;

pub unsafe fn flush_page(addr: VirtAddr) {
    unsafe {
        core::arch::asm!(
            "invlpg [{}]",
            in(reg) addr.0,
            options(nostack, preserves_flags)
        );
    }
}
