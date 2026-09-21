#!/usr/bin/env python3
from __future__ import annotations

import csv
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("phase16_validate", ROOT / "host" / "phase16_validate.py")
assert SPEC and SPEC.loader
validator = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = validator
SPEC.loader.exec_module(validator)

FIELDS = [
    "run_id", "test", "success", "latency_ns", "peak_memory_bytes", "bytes_copied",
    "isolation_violations", "unrecovered_faults", "telemetry_drops",
]


class ValidatorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.cfg = validator.load_config(ROOT / "config" / "phase16_gates.json")
        self.required = list(self.cfg["required_scenarios"])

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def write_csv(
        self,
        name: str,
        runs: int = 30,
        scenarios: list[str] | None = None,
        latency_factor: float = 1.0,
        isolation_violations: int = 0,
        copied_zero_copy: int = 0,
    ) -> Path:
        path = self.dir / name
        scenarios = scenarios or self.required
        with path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            writer.writerow(FIELDS)
            for scenario in scenarios:
                for i in range(runs):
                    writer.writerow([
                        f"{scenario}-{i}",
                        scenario,
                        1,
                        int(100_000 * latency_factor),
                        1_000_000,
                        copied_zero_copy if scenario == "zero_copy_shared_tensor" else 4096,
                        isolation_violations if scenario == "capability_isolation" and i == 0 else 0,
                        0,
                        0,
                    ])
        return path

    def evaluate_files(self, baseline: Path, candidate: Path) -> dict[str, object]:
        return validator.evaluate(
            validator.load_csv(baseline),
            validator.load_csv(candidate),
            self.cfg,
        )

    def test_valid_dataset_passes(self) -> None:
        report = self.evaluate_files(self.write_csv("b.csv"), self.write_csv("c.csv", latency_factor=1.02))
        self.assertEqual(report["status"], "PASS")

    def test_missing_required_scenarios_fails(self) -> None:
        scenarios = ["scheduler"]
        report = self.evaluate_files(
            self.write_csv("b.csv", scenarios=scenarios),
            self.write_csv("c.csv", scenarios=scenarios),
        )
        self.assertEqual(report["status"], "FAIL")
        self.assertTrue(any(c["name"] == "required_scenarios_present" and not c["pass"] for c in report["checks"]))

    def test_insufficient_baseline_fails(self) -> None:
        report = self.evaluate_files(self.write_csv("b.csv", runs=1), self.write_csv("c.csv", runs=30))
        self.assertEqual(report["status"], "FAIL")
        self.assertTrue(any(c["name"] == "baseline_minimum_runs_per_test" and not c["pass"] for c in report["checks"]))

    def test_isolation_violation_fails(self) -> None:
        report = self.evaluate_files(
            self.write_csv("b.csv"),
            self.write_csv("c.csv", isolation_violations=1),
        )
        self.assertEqual(report["status"], "FAIL")

    def test_zero_copy_violation_fails(self) -> None:
        report = self.evaluate_files(
            self.write_csv("b.csv"),
            self.write_csv("c.csv", copied_zero_copy=1),
        )
        self.assertEqual(report["status"], "FAIL")

    def test_duplicate_run_id_rejected(self) -> None:
        path = self.dir / "dup.csv"
        with path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            writer.writerow(FIELDS)
            row = ["same", "scheduler", 1, 100, 1000, 0, 0, 0, 0]
            writer.writerow(row)
            writer.writerow(row)
        with self.assertRaisesRegex(ValueError, "duplicate run_id"):
            validator.load_csv(path)

    def test_invalid_config_rejected(self) -> None:
        cfg_path = self.dir / "bad.json"
        cfg_path.write_text(json.dumps({"schema_version": 1, "minimum_runs_per_test": 1}), encoding="utf-8")
        with self.assertRaises(ValueError):
            validator.load_config(cfg_path)


if __name__ == "__main__":
    unittest.main()
