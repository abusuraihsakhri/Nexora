# Phase 8 restoration notes

The repository import contained `Nexora_Phase_01` through `Nexora_Phase_07` and then resumed at `Nexora_Phase_09`. The original Phase 8 archive was not present in the repository or accessible Project/Library file storage during restoration.

To avoid fabricating an unrelated phase, this directory was reconstructed from three repository-native sources:

1. `Nexora_Phase_07` as the immediate implementation baseline;
2. `Nexora_Phase_07/docs/ROADMAP.md`, whose Milestone 8 explicitly specifies persistent model objects, KV-cache residency, dynamic batching, request deadlines, tensor prefetch, and memory-pressure-aware admission control;
3. `Nexora_Phase_09/docs/PHASE9_INTEGRATION.md`, which documents the Phase 8 model/KV/batching/prefetch/admission-control interfaces that Phase 9 was designed to extend.

The restored implementation is therefore traceable to the repository's own roadmap and downstream integration contract. It is not claimed to be byte-identical to the unavailable historical ZIP.

Validation added as part of restoration:

- strict host behavioral test (`tests/test_phase8.c`);
- freestanding compile/link participation via `src/ai/inference.c`;
- `make phase8-check` symbol verification;
- root GitHub Actions job for Phase 8.
