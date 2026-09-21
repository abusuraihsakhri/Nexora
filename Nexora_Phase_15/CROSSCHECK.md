# Phase 15 Cross-check

Validation performed on the packaged Phase 15 layer:

- `nxbench validate` accepted the deterministic fixture: **360 events**.
- Unit test suite: **6/6 passed**.
- Kernel telemetry adapter compiled with `-std=c11 -Wall -Wextra -Werror -ffreestanding -fno-builtin`.
- Baseline-vs-identical candidate regression comparison: **PASS**.
- Deliberately degraded scheduler metric (+25%): regression gate returned the expected failing status and non-zero exit code.
- Required package files and threshold configuration were checked programmatically.

## Important integration caveat

The Phase 14 cumulative source archive was not available in the current conversation, Project file index, Library search, or connected GitHub repositories. Therefore Phase 15 is packaged as an additive integration layer rather than falsely claiming a merged cumulative Phase 14+15 tree. Kernel integration is isolated behind two explicit hooks documented in `docs/INTEGRATION.md`.
