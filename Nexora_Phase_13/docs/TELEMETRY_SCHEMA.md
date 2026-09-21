# Phase 13 Telemetry Schema Guidance

The Phase 13 core stores numeric metric keys rather than variable-length names. Integrators should maintain a versioned external schema.

Recommended key ranges:

```text
0x0001-0x0fff  kernel/system
0x1000-0x1fff  scheduler/work graph
0x2000-0x2fff  devices/DMA
0x3000-0x3fff  tensor/memory
0x4000-0x4fff  agents/security-facing aggregates
0x5000-0x5fff  distributed/network
0x6000-0x6fff  inference/model runtime
0x7000-0x7fff  reliability/recovery-derived metrics
```

Example initial schema:

```text
0x1001 scheduler.work_submitted_total       counter   count
0x1002 scheduler.work_completed_total       counter   count
0x1003 scheduler.queue_depth                gauge     items
0x1004 scheduler.pick_latency_ns            histogram nanoseconds
0x2001 device.transfer_bytes_total          counter   bytes
0x2002 device.transfer_latency_ns           histogram nanoseconds
0x2003 device.execution_latency_ns          histogram nanoseconds
0x3001 tensor.resident_bytes                gauge     bytes
0x5001 remote.transfer_bytes_total          counter   bytes
0x5002 remote.roundtrip_latency_ns          histogram nanoseconds
0x7001 recovery.latency_ns                  histogram nanoseconds
```

## Cardinality rule

Do not create a metric for every transient request, tensor, or token. High-cardinality identity belongs in bounded traces; metrics should use stable resource/domain scopes.

## Privacy/security rule

Keys and trace arguments should describe execution metadata. Do not encode prompts, filenames containing secrets, credentials, capability tokens, model weights, raw virtual/physical addresses, or user payload bytes into numeric arguments.
