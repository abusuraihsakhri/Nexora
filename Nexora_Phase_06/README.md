# Nexora Phase 6 — Host-Side Comparison Harness

Phase 6 turns Nexora's architectural hypotheses into reproducible Linux host-side baselines and a strict matched-parameter path for later Nexora comparisons.

The harness implements the five comparisons named in the original roadmap:

1. allocation latency
2. graph scheduling overhead
3. tensor lifetime memory peak
4. zero-copy IPC
5. deadline tail latency

It deliberately does **not** claim that Nexora is faster than Linux. Linux measurements can be produced now; a Nexora-vs-Linux conclusion requires Phase-5 kernel telemetry collected under matched benchmark parameters.

## Quick start

```bash
make
make test
python3 scripts/run_suite.py --quick --repetitions 3 --output results/quick
python3 scripts/analyze.py results/quick/raw.jsonl --out-dir results/quick/analysis
```

The generated report is:

```text
results/quick/analysis/report.md
```

## Full run

```bash
python3 scripts/run_suite.py --repetitions 15 --output results/full
python3 scripts/analyze.py results/full/raw.jsonl --out-dir results/full/analysis
```

For lower jitter, pin the harness to one CPU when permitted:

```bash
python3 scripts/run_suite.py --repetitions 15 --cpu 2 --output results/pinned
```

## Add Nexora measurements

Phase-5/Phase-6 kernel instrumentation should emit serial lines beginning with:

```text
NEXORA_BENCH {JSON object}
```

The bundled `results/example/nexora_serial.log` is explicitly marked as a synthetic parser fixture and is rejected by the analyzer as benchmark evidence. Extract real kernel telemetry with:

```bash
python3 scripts/ingest_nexora_serial.py qemu-serial.log > results/nexora.jsonl
```

Then analyze Linux and Nexora together. The report includes a comparability audit and will warn if the two platforms do not contain identical benchmark-ID/parameter groups:

```bash
cat results/full/raw.jsonl results/nexora.jsonl > results/combined.jsonl
python3 scripts/analyze.py results/combined.jsonl --out-dir results/combined-analysis
```

See `docs/TELEMETRY_PROTOCOL.md` and `docs/PHASE6_EXECUTION.md`.

## Repository layout

```text
bench/src/                  Linux C benchmark implementations
scripts/run_suite.py        reproducible benchmark orchestrator
scripts/analyze.py          cross-run aggregation and Markdown/CSV/JSON output
scripts/ingest_nexora_serial.py
                            serial telemetry extractor/validator
tests/                      unit + executable smoke tests
docs/                       protocol, methodology, integration and acceptance criteria
```

## Phase-6 acceptance condition

Phase 6 is complete when:

- all five Linux baseline benchmark families compile and execute;
- every result validates against the canonical telemetry fields;
- repeated runs produce raw machine-readable records plus aggregate CSV/JSON/Markdown;
- Nexora serial telemetry can be ingested without hand editing;
- matched-parameter comparisons are possible without changing the analysis code;
- the harness never fabricates a Nexora result when no kernel measurement exists.
