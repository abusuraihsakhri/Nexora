# Nexora Phase 4 — Complete Package

This archive contains the consolidated Phase 4 source tree as of Step 10, all Phase 4 documentation and validation/benchmark artifacts, plus the per-step ZIP snapshots from Steps 2–10 under `history/`.

## Phase 4 sequence

1. Shared tensor backing model / architectural split (captured by the final backing/tensor implementation and Phase 4 documentation)
2. Work domains
3. Per-domain opaque handle tables
4. Handle access rights
5. Cross-domain sharing and transfer
6. Zero-copy backing and shared mappings
7. Reference counting and lifetime management
8. Revocation and concurrency hardening
9. Adversarial security/correctness testing
10. Benchmark and full integration gate

## Primary consolidated tree

Use the files in this directory (`include/`, `src/`, `tests/`, `docs/`, `Makefile`, etc.) as the current Phase 4 codebase. The `history/` directory exists only to preserve the incremental step snapshots.

See `docs/PHASE4_COMPLETE.md` and `VALIDATION.txt` for the completion summary and validation record.
