#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ThreadState {
    New,
    Runnable,
    Running,
    Blocked,
    Sleeping,
    Exited,
}
