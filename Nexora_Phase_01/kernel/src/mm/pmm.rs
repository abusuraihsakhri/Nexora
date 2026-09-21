use super::address::PhysAddr;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PhysFrame {
    pub start: PhysAddr,
}

pub trait PhysicalMemoryManager {
    fn alloc_frame(&mut self) -> Option<PhysFrame>;
    fn alloc_contiguous(
        &mut self,
        count: usize,
        alignment_pages: usize,
    ) -> Option<PhysFrame>;
    fn free_frame(&mut self, frame: PhysFrame);
    fn reserve_range(&mut self, start: u64, length: u64);
    fn total_frames(&self) -> usize;
    fn free_frames(&self) -> usize;
}
