import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("nxbench", ROOT / "tools" / "nxbench.py")
nx = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(nx)


def event(i, value, benchmark="scheduler.dispatch", metric="latency_ns", unit="ns", variant="graph", workload="mixed"):
    return {
        "schema": "nexora.bench.v1",
        "run_id": "t",
        "benchmark": benchmark,
        "metric": metric,
        "value": value,
        "unit": unit,
        "iteration": i,
        "variant": variant,
        "workload": workload,
    }


class NxBenchTests(unittest.TestCase):
    def test_quantile_linear(self):
        self.assertEqual(nx.quantile([1, 2, 3, 4, 5], 0.5), 3)
        self.assertAlmostEqual(nx.quantile([0, 10], 0.95), 9.5)

    def test_summary(self):
        s = nx.summarize_events([event(i, i + 1) for i in range(40)])
        m = next(iter(s["metrics"].values()))
        self.assertEqual(m["count"], 40)
        self.assertEqual(m["median"], 20.5)
        self.assertAlmostEqual(m["p95"], 38.05)

    def test_compare_pass_and_fail(self):
        base = nx.summarize_events([event(i, 100) for i in range(40)])
        good = nx.summarize_events([event(i, 105) for i in range(40)])
        bad = nx.summarize_events([event(i, 120) for i in range(40)])
        thresholds = {"defaults": {"direction": "lower", "statistic": "median", "max_regression_pct": 10, "min_samples": 30, "required": True}, "rules": []}
        self.assertEqual(nx.compare_summaries(base, good, thresholds)["status"], "pass")
        self.assertEqual(nx.compare_summaries(base, bad, thresholds)["status"], "fail")

    def test_missing_candidate_is_failure(self):
        base = nx.summarize_events([event(i, 100) for i in range(40)])
        cand = {"schema": "nexora.bench.summary.v1", "run_ids": [], "metrics": {}}
        thresholds = {"defaults": {"required": True, "min_samples": 1}, "rules": []}
        self.assertEqual(nx.compare_summaries(base, cand, thresholds)["status"], "fail")

    def test_extract_mixed_serial(self):
        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / "serial.log"
            out = Path(td) / "events.jsonl"
            payload = json.dumps(event(0, 77))
            src.write_text("booting\n[serial] " + payload + "\nnot json\n", encoding="utf-8")
            self.assertEqual(nx.extract_events(src, out), 1)
            self.assertEqual(len(nx.read_events(out)), 1)

    def test_invalid_event(self):
        with self.assertRaises(ValueError):
            nx.validate_event({"schema": "wrong"})


if __name__ == "__main__":
    unittest.main()
