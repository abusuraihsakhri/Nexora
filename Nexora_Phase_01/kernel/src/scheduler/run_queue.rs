use crate::task::id::ThreadId;

pub struct RunQueue {
    // Intrusive implementation should replace this placeholder.
    pub head: Option<ThreadId>,
    pub tail: Option<ThreadId>,
    pub len: usize,
}
