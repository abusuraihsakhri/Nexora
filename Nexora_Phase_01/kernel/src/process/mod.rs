use crate::{
    mm::pmm::PhysFrame,
    task::id::{ProcessId, ThreadId},
};

pub struct AddressSpace {
    pub root: PhysFrame,
}

pub enum ProcessState {
    New,
    Running,
    Exiting,
    Exited,
}

pub struct Process {
    pub id: ProcessId,
    pub address_space: AddressSpace,
    pub state: ProcessState,
    pub exit_code: Option<i32>,
    pub main_thread: Option<ThreadId>,
}
