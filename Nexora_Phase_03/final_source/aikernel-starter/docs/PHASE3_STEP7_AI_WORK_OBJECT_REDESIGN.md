# Phase 3 Step 7 — AI Work-Object Redesign

## Goal

Step 7 turns a work node from a mostly generic dependency record into a kernel-visible
AI execution object. The kernel can now distinguish *what kind of work it is*, *what
service intent it carries*, and *what resource cost the submitter expects* before any
real CPU/GPU/NPU backend exists.

This step deliberately does **not** implement device placement. Step 8 will consume the
metadata added here together with locality and device-residency information.

## New descriptor-based API

Work creation now has a fallible descriptor path:

```c
ai_work_status ai_work_try_create(
    ai_work_graph *graph,
    const ai_work_desc *desc,
    ai_work_node **out_node
);
```

and a panic-on-programmer-error convenience wrapper:

```c
ai_work_node *ai_work_create(
    ai_work_graph *graph,
    const ai_work_desc *desc
);
```

The original `ai_work_add()` API remains available and maps to a default descriptor so
existing graph code remains source compatible.

## Work semantics

Each node now records:

```text
operation
work class
QoS class
priority
deadline
allowed device classes
estimated operations
estimated read bytes
estimated write bytes
estimated scratch bytes
estimated duration
logical input bytes
logical output bytes
batch ID
batch size
execution flags
```

### Work classes

```text
COMPUTE
MEMORY
TRANSFER
CONTROL
CUSTOM
```

The work class is intentionally independent from the operation opcode. For example,
two custom operators may have very different resource behavior.

### QoS classes

```text
DEFAULT
LATENCY
THROUGHPUT
BACKGROUND
```

These are submitter-provided service intents. They are not inferred from tensor names
or operation types.

### Execution flags

Step 7 defines metadata for future scheduler/runtime policy:

```text
AUTO_IO_BYTES
PREEMPTIBLE
BATCHABLE
DETERMINISTIC
IDEMPOTENT
```

These flags do not claim capabilities the kernel cannot yet enforce. In particular,
`PREEMPTIBLE` is a declaration that future execution backends may use when safe
preemption points exist.

## Cost model

A work node now has a compact explicit cost profile:

```c
u64 estimated_ops;
u64 estimated_read_bytes;
u64 estimated_write_bytes;
u64 estimated_scratch_bytes;
u64 estimated_duration_ns;
```

A zero estimate means unknown/not supplied.

When `AI_WORK_FLAG_AUTO_IO_BYTES` is set, read/write estimates are derived from tensor
edges:

```text
estimated_read_bytes  = sum(input.logical_bytes)
estimated_write_bytes = sum(output.logical_bytes)
```

The node also stores the exact logical edge totals separately:

```text
logical_input_bytes
logical_output_bytes
```

This separation matters because a real operator may read the same tensor repeatedly,
use tiling, or generate extra transfer traffic. Therefore logical edge size and expected
memory traffic are related but not universally identical.

For explicit estimates, `AUTO_IO_BYTES` is disabled and attaching tensors does not
overwrite the supplied estimates.

All logical-byte accumulation is overflow checked. An attachment that would overflow
`u64` is rejected before the graph edge is published.

## Batch identity

Work objects can now carry:

```c
u64 batch_id;
u32 batch_size;
```

`batch_id == 0` means no batch assignment. In that state `batch_size` must be one.
Step 7 stores this metadata only; batching and coalescing policy are future work.

## Validation

Descriptor creation rejects:

- invalid operation/class/QoS values
- priority above `AI_WORK_PRIORITY_MAX`
- empty or unknown device masks
- unknown work flags
- invalid batch metadata
- simultaneous AUTO I/O estimation and explicit read/write estimates
- graph exhaustion
- object-registry exhaustion
- allocator exhaustion

The fallible constructor never publishes a partially initialized work node.

## Scheduler integration

The scheduler is still deliberately small. Step 7 makes it consume the richer metadata
without attempting Step 8 placement logic.

Ready nodes are ordered by:

```text
1. higher explicit priority
2. deadline-bearing work before no-deadline work
3. earlier deadline
4. QoS intent: latency > default > throughput > background
5. shorter known duration when both candidates provide estimates
6. lower object ID for deterministic tie-breaking
```

A deadline value of zero is now explicitly treated as **no deadline** rather than as an
artificially earliest deadline.

Transfer/locality costs are intentionally absent from this policy until Step 8 has a
real placement model.

## Backward compatibility

The legacy constructor:

```c
ai_work_add(graph, name, op, priority, deadline, device_mask)
```

maps to:

```text
class       = COMPUTE (TRANSFER for AI_OP_TRANSFER, CONTROL for NOOP)
qos         = DEFAULT
batch       = none / size 1
I/O bytes   = auto-derived from tensor edges
cost hints  = unknown
```

Step 6 producer/consumer semantics and final-consumer reclamation remain unchanged.

## Tests

A new host test, `tests/work_object_redesign_test.c`, verifies:

- descriptor construction
- work class/QoS storage
- batch metadata
- automatic input/output byte derivation
- explicit traffic estimates
- scratch-byte accounting
- invalid estimate configuration rejection
- invalid batch rejection
- invalid device-mask rejection
- no-deadline semantics
- deadline scheduling precedence
- QoS tie-breaking
- duration tie-breaking

Run:

```bash
make test-work
```

or the complete suite:

```bash
make test
```

## Step 7 boundary

This step answers:

> What does the kernel know about a unit of AI work before it dispatches it?

It does not yet answer:

> Where should that work run?

That is Phase 3 Step 8: locality and device constraints/placement.
