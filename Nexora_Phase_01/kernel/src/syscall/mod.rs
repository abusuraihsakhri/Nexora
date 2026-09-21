#[repr(u64)]
#[derive(Clone, Copy, Debug)]
pub enum SyscallNumber {
    Exit = 0,
    Write = 1,
    Yield = 2,
    GetTime = 3,
}

#[repr(i64)]
#[derive(Clone, Copy, Debug)]
pub enum Errno {
    InvalidArgument = 1,
    BadAddress = 2,
    NotSupported = 3,
    NotFound = 4,
    OutOfMemory = 5,
}

#[derive(Clone, Copy, Debug)]
pub struct UserAddr(pub u64);

pub fn dispatch(number: u64, _args: [u64; 6]) -> i64 {
    match number {
        x if x == SyscallNumber::Exit as u64 => 0,
        x if x == SyscallNumber::Write as u64 => 0,
        x if x == SyscallNumber::Yield as u64 => 0,
        x if x == SyscallNumber::GetTime as u64 => 0,
        _ => -(Errno::NotSupported as i64),
    }
}
