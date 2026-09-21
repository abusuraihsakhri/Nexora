# Phase 3 Step 6 — Producer/Consumer Graph

## Objective

Step 6 makes tensor dataflow visible to the kernel. A tensor can now record one
producer work object and a bounded set of unique consumer work objects. This lets
Nexora answer three questions without relying on application-side allocator state:

1. Which work node created this tensor?
2. Which work nodes can still read it?
3. Has its final consumer completed?

Those answers are the prerequisite for graph-aware scheduling and early tensor
reclamation.

## Data model

Each `ai_tensor` now contains non-owning work-object handles:

```c
nx_handle_t producer_work;
nx_handle_t consumers[AI_MAX_TENSOR_CONSUMERS];
u64 consumer_done_mask;
u32 consumer_count;
u32 consumers_remaining;
```

The consumer limit is currently 32. The done bitmask makes consumer completion
idempotence explicit and avoids decrementing `consumers_remaining` twice.

The relationship is:

```text
Work P
  |
  | produces
  v
Tensor T
  |
  +----> Work C1
  +----> Work C2
  +----> Work C3

consumer_count      = 3
consumers_remaining = 3
```

After C1 and C2 complete:

```text
consumer_count      = 3
consumers_remaining = 1
```

After C3 completes, the tensor receives a final-consumer event.

## Ownership rule: no work/tensor reference cycle

The graph deliberately uses asymmetric ownership:

```text
Work node --strong reference--> Tensor
Tensor    --non-owning handle--> Work node
```

A work node retains every tensor attached as an input or output. The tensor does
not retain its producer or consumers. This prevents a cycle such as:

```text
Work -> Tensor -> Work
```

which would otherwise prevent deterministic reference-count destruction.

When a graph is destroyed, work-object destructors:

1. remove their non-owning consumer/producer metadata from tensors;
2. release the work-held tensor references;
3. allow normal tensor destruction if no other owner remains.

## Producer registration

`ai_work_try_add_output(node, tensor)` establishes the producer relationship.
A tensor may have at most one producer.

A second producer is rejected with:

```text
AI_WORK_ERR_PRODUCER_EXISTS
```

A work node is also prevented from declaring the same tensor as both its own
input and output, avoiding an immediate self-dependency in this v0 graph model.
In-place operations can be introduced later with explicit version semantics.

## Consumer registration

`ai_work_try_add_input(node, tensor)`:

1. rejects duplicate input edges for the same work node;
2. records the work handle in the tensor consumer set;
3. increments `consumers_remaining`;
4. acquires a strong reference from the work node to the tensor.

This means consumer cardinality is the number of unique work nodes, not the
number of repeated argument positions within one operation.

## Data dependencies are scheduler-visible

An explicit dependency ID is no longer required for the common producer -> tensor
-> consumer path.

For every work input, `ai_work_dependencies_done()` checks whether that tensor
has a producer. If it does, the producer work object must be `AI_WORK_DONE`
before the consumer can transition from `PENDING` to `READY`.

Example:

```text
Work A --produces--> Tensor X --consumed by--> Work B
```

Before A completes:

```text
A = READY/RUNNING
B = PENDING
```

After A completes:

```text
A = DONE
B = READY
```

Explicit control dependencies remain supported for ordering requirements that do
not correspond to tensor flow.

## Completion and final-consumer accounting

`ai_work_complete()` performs an all-or-nothing preflight of its input consumer
edges, marks the work node `DONE`, and then marks each input as consumed exactly
once.

For every input whose remaining-consumer counter reaches zero:

```text
consumers_remaining: 1 -> 0
              |
              v
      final-consumer event
```

The graph records this event even when the tensor lifetime policy does not allow
automatic reclamation.

## Initial connection to the lifetime engine

For a resident `TEMPORARY` tensor, a final-consumer event currently invokes:

```c
ai_tensor_lifetime_reclaim(tensor, AI_RECLAIM_FINAL_CONSUMER);
```

Therefore this path is now executable:

```text
Temporary tensor
      |
      v
last consumer completes
      |
      v
consumers_remaining == 0
      |
      v
AI_RECLAIM_FINAL_CONSUMER
      |
      v
backing detached / memory object released
      |
      v
Tensor metadata remains valid
```

This is intentionally the first, narrow automatic-reclaim hook. Phase 3 Step 9
will turn it into the full reclamation subsystem, including deferred retry,
pressure integration, and broader policy handling.

Persistent, cached, shared, and external tensors are not automatically stripped
by this Step 6 hook merely because they reached zero consumers.

## Graph statistics

Each `ai_work_graph` tracks:

```text
nodes_created
producer_links
consumer_links
consumer_completions
final_consumer_events
automatic_reclaim_attempts
automatic_reclaim_successes
automatic_reclaim_deferred
```

This gives later experiments a direct way to compare graph semantics with memory
behavior.

## Public interfaces added

Fallible graph construction:

```c
ai_work_try_add_dependency(...)
ai_work_try_add_input(...)
ai_work_try_add_output(...)
```

Completion:

```c
ai_work_complete(...)
ai_scheduler_complete(...)
```

Tensor provenance/consumer inspection:

```c
ai_tensor_producer_work(...)
ai_tensor_consumer_count(...)
ai_tensor_consumers_remaining(...)
ai_tensor_has_consumer(...)
ai_tensor_consumer_done(...)
```

Graph lifecycle/statistics:

```c
ai_work_graph_destroy(...)
ai_work_graph_get_stats(...)
```

The original panic-on-error `ai_work_add_*()` calls remain as convenience wrappers
for trusted kernel construction code.

## Test scenario

`tests/producer_consumer_graph_test.c` builds this graph:

```text
source ----\
            Work P ----> hidden ----> Work C1 ----> out1
weight ----/                    \\---> Work C2 ----> out2
```

The test verifies:

- one-producer enforcement;
- duplicate-consumer rejection;
- scheduler readiness derived from tensor provenance;
- two-consumer remaining-count behavior;
- no reclamation after only the first consumer;
- final-consumer reclamation after the second consumer;
- persistent-weight survival;
- graph event/reclaim statistics;
- graph teardown removing provenance edges;
- work-held tensor references being released correctly.

## Current limitations

- Consumer fan-out is statically bounded at 32.
- In-place operations are not represented yet; they need explicit tensor versioning.
- Failed/cancelled work does not yet resolve consumer obligations.
- A final-consumer reclaim that is blocked by a pin is counted as deferred but is
  not retried automatically yet. Step 9 will own that mechanism.
- The backing memory object can be logically destroyed, but the bootstrap bump
  allocator still cannot return its physical pages to a reusable free list.
- Producer relationships are data dependencies; cycle detection for arbitrary
  explicit dependency graphs is not implemented yet.

These limits are explicit so the graph semantics remain testable rather than
pretending that cancellation, mutation, or real page recycling already exist.
