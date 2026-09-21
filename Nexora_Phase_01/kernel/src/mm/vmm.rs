use super::{
    address::{PhysAddr, VirtAddr},
    pmm::PhysFrame,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Page {
    start: VirtAddr,
}

impl Page {
    pub fn from_start_address(addr: VirtAddr) -> Option<Self> {
        addr.is_page_aligned().then_some(Self { start: addr })
    }

    pub const fn start_address(self) -> VirtAddr {
        self.start
    }
}

#[derive(Clone, Copy, Debug)]
pub struct MapFlags {
    pub read: bool,
    pub write: bool,
    pub execute: bool,
    pub user: bool,
    pub global: bool,
}

impl MapFlags {
    pub const fn kernel_data() -> Self {
        Self {
            read: true,
            write: true,
            execute: false,
            user: false,
            global: true,
        }
    }
}

#[derive(Debug)]
pub enum MapError {
    AlreadyMapped,
    Unaligned,
    WritableExecutable,
    OutOfMemory,
}

#[derive(Debug)]
pub enum UnmapError {
    NotMapped,
}

pub trait VirtualMemoryManager {
    fn map_4k(
        &mut self,
        page: Page,
        frame: PhysFrame,
        flags: MapFlags,
    ) -> Result<(), MapError>;

    fn unmap_4k(&mut self, page: Page) -> Result<PhysFrame, UnmapError>;

    fn translate(&self, addr: VirtAddr) -> Option<PhysAddr>;
}
