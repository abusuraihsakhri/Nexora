#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
python3 tools/generate_fixture.py >/dev/null
python3 tools/nxbench.py validate fixtures/sample_run.jsonl
python3 tools/nxbench.py summarize fixtures/sample_run.jsonl --output results/sample_summary.json
cp results/sample_summary.json fixtures/baseline_summary.json
python3 tools/nxbench.py compare fixtures/baseline_summary.json results/sample_summary.json \
  --thresholds config/thresholds.json --output results/comparison.json
python3 -m unittest discover -s tests -v
python3 tests/check_package.py
printf 'Phase 15 self-test: PASS\n'
