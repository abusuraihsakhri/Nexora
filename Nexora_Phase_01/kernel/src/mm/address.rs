#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
#[repr(transparent)]
pub struct PhysAddr(pub u64);

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
#[repr(transparent)]
pub struct VirtAddr(pub u64);

impl VirtAddr {
    pub const fn is_page_aligned(self) -> bool {
        self.0 & 0xfff == 0
    }

    pub const fn page_offset(self) -> u64 {
        self.0 & 0xfff
    }

    pub const fn p1_index(self) -> usize {
        ((self.0 >> 12) & 0x1ff) as usize
    }

    pub const fn p2_index(self) -> usize {
        ((self.0 >> 21) & 0x1ff) as usize
    }

    pub const fn p3_index(self) -> usize {
        ((self.0 >> 30) & 0x1ff) as usize
    }

    pub const fn p4_index(self) -> usize {
        ((self.0 >> 39) & 0x1ff) as usize
    }
}
