# Nexora AIKernel — Phase 8

**Milestone:** Inference-oriented experiments  
**Baseline:** Complete Phase 7 device-path tree  
**Status:** Restored, implemented, and host-tested

Phase 8 extends the Phase 7 hardware-facing accelerator path with the inference orchestration semantics specified by the original Nexora roadmap.

## Phase 8 additions

- persistent model objects and residency state
- KV-cache/session residency metadata
- memory-pressure-aware request admission
- deadline-aware dynamic batching
- request lifecycle/accounting
- tensor prefetch planning
- deterministic host behavioral tests
- freestanding kernel compile/link integration
- `make phase8-check` validation target

The Phase 7 PCI/DMA/simulated-accelerator implementation is retained intact underneath this layer.

## Architecture boundary

Phase 8 is a research control plane. It models the decisions an AI-native operating system can make before dispatch:

```text
request
  │
  ├── model residency
  ├── KV state
  ├── deadline
  ├── working-memory estimate
  │
  ▼
admission control
  │
  ▼
deadline-aware dynamic batch
  │
  ▼
prefetch / placement intent
  │
  ▼
Phase 7 accelerator path
```

It does **not** claim to implement vLLM, CUDA, ROCm, PJRT, a tokenizer, real GPU kernels, or production KV-cache allocation.

## Verify

Host behavioral test:

```sh
make phase8-host-test
```

Freestanding kernel build + symbol checks + host test:

```sh
make phase8-check
```

Full scripted cross-check:

```sh
./scripts/phase8_crosscheck.sh
```

## Key files

- `include/ai/inference.h` — Phase 8 public API
- `src/ai/inference.c` — bounded inference orchestration implementation
- `tests/test_phase8.c` — deterministic behavioral test
- `docs/PHASE8_INFERENCE_EXPERIMENTS.md` — architecture, invariants, and limits
- `docs/PHASE8_RESTORATION_NOTES.md` — provenance of the repository restoration
- `docs/ROADMAP.md` — roadmap with Milestone 8 marked implemented

## Restoration provenance

The historical Phase 8 ZIP was not present in the repository or accessible Project/Library file storage during restoration. This directory was reconstructed from the repository's own Phase 7 baseline, Milestone 8 roadmap definition, and Phase 9 integration contract. It is therefore traceable and testable, but is not claimed to be byte-identical to the unavailable historical archive.
