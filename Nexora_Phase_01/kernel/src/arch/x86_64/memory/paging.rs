use crate::mm::address::PhysAddr;

pub const ENTRY_COUNT: usize = 512;

#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct PageTableEntry(pub u64);

impl PageTableEntry {
    pub const fn unused() -> Self {
        Self(0)
    }

    pub fn is_present(self) -> bool {
        self.0 & 1 != 0
    }

    pub fn addr(self) -> PhysAddr {
        PhysAddr(self.0 & 0x000f_ffff_ffff_f000)
    }

    pub fn clear(&mut self) {
        self.0 = 0;
    }
}

#[repr(C, align(4096))]
pub struct PageTable {
    pub entries: [PageTableEntry; ENTRY_COUNT],
}
