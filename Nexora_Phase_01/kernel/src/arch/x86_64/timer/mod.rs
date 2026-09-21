use crate::time::Instant;

pub trait KernelTimer {
    fn now(&self) -> Instant;
    fn set_deadline(&mut self, deadline: Instant) -> Result<(), TimerError>;
    fn cancel_deadline(&mut self);
}

#[derive(Debug)]
pub enum TimerError {
    Unsupported,
    InvalidDeadline,
}
