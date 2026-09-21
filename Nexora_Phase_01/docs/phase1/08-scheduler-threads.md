# Step 8 — Preemptive Scheduler and Kernel Threads

## Objective

Turn Nexora into a multitasking kernel.

## Architecture

Design per-CPU scheduling state from the beginning:

```text
CPU0 → local run queue + current + idle
CPU1 → local run queue + current + idle
...
```

Phase 1 activates one CPU, but should not require a global-run-queue redesign for SMP.

## Thread states

```text
New
Runnable
Running
Blocked
Sleeping
Exited
```

State transitions must be explicit.

## Thread control block

Track:

- ThreadId;
- state;
- CPU context;
- kernel stack;
- entry function;
- priority placeholder;
- CPU affinity;
- last CPU;
- runtime accounting;
- wake deadline.

Kernel stacks should be explicit VMM/PMM mappings with guard pages.

## Context switching

Save the required x86-64 callee-saved state for voluntary switches and converge preemptive switching with the interrupt `TrapFrame` model.

## Policy

Phase 1:

```text
round robin
```

Use a fixed timeslice initially, e.g. ~4–5 ms, but build on one-shot deadlines rather than a permanent periodic tick.

## Tickless foundation

Next hardware deadline is the minimum of:

- current timeslice end;
- earliest sleeping-thread wake;
- next kernel timer.

## Required operations

- spawn kernel thread;
- yield;
- preempt;
- sleep;
- block;
- wake;
- exit;
- deferred reclamation;
- idle thread with HLT.

## Preemption control

Use a nesting counter, not a boolean:

```text
0 → preemptible
>0 → preemption disabled
```

A pending reschedule flag handles events that occur while preemption is disabled.

## Acceptance tests

- cooperative A/B/C switching;
- register preservation;
- stack isolation;
- preemption of non-yielding thread;
- sleep deadlines;
- block/wake;
- thread exit and stack reclamation;
- 100k+ context switches;
- scheduler integrity validator.
