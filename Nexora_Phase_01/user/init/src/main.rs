#![no_std]
#![no_main]

use core::panic::PanicInfo;

const SYS_EXIT: u64 = 0;
const SYS_WRITE: u64 = 1;

#[unsafe(no_mangle)]
pub extern "C" fn _start() -> ! {
    let msg = b"Hello from Nexora userspace\n";

    unsafe {
        syscall3(
            SYS_WRITE,
            1,
            msg.as_ptr() as u64,
            msg.len() as u64,
        );

        syscall1(SYS_EXIT, 0);
    }

    loop {
        core::hint::spin_loop();
    }
}

unsafe fn syscall1(number: u64, a0: u64) -> i64 {
    let result: i64;
    unsafe {
        core::arch::asm!(
            "syscall",
            inlateout("rax") number => result,
            in("rdi") a0,
            lateout("rcx") _,
            lateout("r11") _,
        );
    }
    result
}

unsafe fn syscall3(number: u64, a0: u64, a1: u64, a2: u64) -> i64 {
    let result: i64;
    unsafe {
        core::arch::asm!(
            "syscall",
            inlateout("rax") number => result,
            in("rdi") a0,
            in("rsi") a1,
            in("rdx") a2,
            lateout("rcx") _,
            lateout("r11") _,
        );
    }
    result
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}
