use super::{id::ThreadId, state::ThreadState};

pub type ThreadEntry = extern "C" fn(usize) -> !;

pub struct Thread {
    pub id: ThreadId,
    pub state: ThreadState,
    pub runtime_ns: u64,
    pub entry: ThreadEntry,
    pub arg: usize,
}
