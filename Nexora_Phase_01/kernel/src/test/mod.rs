pub enum TestResult {
    Pass,
    Fail(&'static str),
}

pub struct KernelTest {
    pub name: &'static str,
    pub run: fn() -> TestResult,
}

// TODO: add PMM/VMM/heap/scheduler/process/syscall stress suites.
