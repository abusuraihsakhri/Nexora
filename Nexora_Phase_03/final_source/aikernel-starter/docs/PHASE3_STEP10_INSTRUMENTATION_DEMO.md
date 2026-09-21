# Phase 3 Step 10 — Instrumentation and Reproducible Demo

Phase 3 Step 10 closes the first AI-object-model milestone by making the behavior measurable.
The kernel now records bounded runtime trace events, can dump graph/tensor/memory state to the
serial console, and includes a host-side experiment comparing graph-aware final-consumer
reclamation against an otherwise identical lifetime-blind baseline.

## 1. Goals

Step 10 provides four things:

1. a bounded, allocation-free trace buffer suitable for the freestanding kernel;
2. graph, tensor, trace, and memory-summary diagnostic dumps;
3. event hooks across tensor backing, work dispatch/completion, and reclamation;
4. a reproducible peak-live-resident-memory experiment.

The instrumentation intentionally avoids dynamic allocation so that observing the workload does
not create a second memory-management workload.

## 2. Trace model

`include/ai/instrument.h` defines a fixed-capacity trace of 512 events. Each event contains:

```text
sequence
kind
object_id
related_id
value
resident_bytes
```

The `resident_bytes` field snapshots `nx_memory_stats.resident_bytes` at the event. This makes the
memory timeline inspectable without requiring a timer or wall-clock source, which the current
kernel still lacks.

Current event classes are:

```text
tensor-create
tensor-bind
tensor-unbind
tensor-destroy
work-create
work-dispatch
work-complete
tensor-reclaim
reclaim-defer
memory-sample
```

The trace is bounded. When full, new events are dropped and `events_dropped` is incremented;
existing events are never overwritten silently.

## 3. Console diagnostics

`include/ai/debug.h` / `src/ai/debug.c` add:

```c
ai_debug_dump_tensor(...)
ai_debug_dump_tensors()
ai_debug_dump_graph(...)
ai_debug_dump_trace(...)
ai_debug_dump_memory_summary()
```

The boot demo resets the trace before its workload, lazily allocates work outputs, executes the
small graph, and emits these dumps over the existing VGA/serial console.

## 4. Benchmark control

The reclamation manager now has a narrow experimental switch:

```c
ai_reclaim_set_final_consumer_enabled(bool enabled);
```

It defaults to enabled. It exists to create an A/B experiment using the same graph and allocator:

- **baseline:** final-consumer reclamation disabled;
- **graph-aware:** final-consumer reclamation enabled.

This switch does not alter pressure reclamation or lifetime semantics. It only suppresses the
final-consumer automatic-reclamation path for the baseline run.

## 5. Reproducible experiment

Run:

```bash
make test-instrument
```

The test constructs the same three-stage CPU graph in both conditions:

```text
input ----\
           stage-1 ---> hidden-1 ---\
weights --/                          stage-2 ---> hidden-2 ---\
weights ----------------------------/                       stage-3 ---> output
weights ---------------------------------------------------/
```

Tensor storage sizes are exact page multiples:

```text
input      96 KiB   temporary
weights   128 KiB   persistent
hidden-1  160 KiB   temporary
hidden-2  160 KiB   temporary
output     64 KiB   persistent
```

Inputs and weights begin resident. Each work output is allocated lazily immediately before its
producer dispatch. Therefore dead intermediates contribute to subsequent peaks only when they
are not reclaimed.

Measured in the bundled host test:

```text
baseline peak resident:       622592 bytes  (608 KiB)
graph-aware peak resident:    458752 bytes  (448 KiB)
peak reduction:               163840 bytes  (160 KiB, 26.31%)
graph-aware early reclaimed:  425984 bytes  (416 KiB)
graph-aware final resident:   196608 bytes  (192 KiB)
trace events:                 32
```

The peak reduction is exactly the live-resident accounting effect of reclaiming graph-dead
intermediates before later outputs are allocated in this synthetic workload.

## 6. Interpretation limits

The result is deliberately narrow.

It demonstrates that, for this controlled graph and allocation schedule, kernel-visible
producer/consumer lifetimes reduce **peak live resident accounting** relative to a baseline that
retains dead tensors.

It does **not** yet demonstrate:

- lower physical-RAM high-water usage in the bootstrap arena;
- reusable reclaimed pages;
- GPU HBM savings on real hardware;
- wall-clock latency or throughput improvements;
- superiority over mature framework allocators.

The early heap remains monotonic. Destroyed `NX_OBJECT_MEMORY` objects leave live-resident
accounting, but their backing pages are not returned to a reusable free list. A page-frame
allocator is required before the experiment can claim physical-page reuse.

## 7. Phase 3 outcome

Phase 3 now contains an end-to-end path:

```text
Tensor identity + metadata
        -> memory backing
        -> ownership/refcounts
        -> lifetime policy
        -> producer/consumer graph
        -> AI-native work metadata
        -> locality/device placement
        -> automatic reclamation
        -> instrumentation + measurement
```

This is the first complete Nexora architectural experiment. The next milestone should strengthen
the substrate required to convert logical reclamation into reusable physical memory and then
compare against host/Linux baselines under realistic workload traces.
