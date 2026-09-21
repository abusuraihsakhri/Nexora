use crate::task::id::ThreadId;

#[derive(Clone, Copy, Debug)]
pub enum ScheduleReason {
    Yield,
    TimesliceExpired,
    Block,
    Sleep,
    Exit,
    WakeupPreemption,
}

pub struct PerCpuScheduler {
    pub current: Option<ThreadId>,
    pub idle: ThreadId,
    pub preempt_disable_count: u32,
    pub need_reschedule: bool,
}
