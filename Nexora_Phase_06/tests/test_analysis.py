import importlib.util, json, tempfile, unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("analyze",ROOT/"scripts"/"analyze.py")
analyze=importlib.util.module_from_spec(spec); spec.loader.exec_module(analyze)

class AnalysisTests(unittest.TestCase):
    def test_aggregate(self):
        recs=[]
        for x in [10,20,30]:
            recs.append({"schema":"nexora.bench.v1","benchmark":"b","platform":"linux","variant":"v","params":{"n":1},"metrics":{"latency_ns":x}})
        rows=analyze.aggregate(recs)
        self.assertEqual(len(rows),1); self.assertEqual(rows[0]["median"],20); self.assertEqual(rows[0]["mean"],20)

    def test_round_trip_output(self):
        rec={"schema":"nexora.bench.v1","benchmark":"b","platform":"linux","variant":"v","params":{},"metrics":{"x":1}}
        with tempfile.TemporaryDirectory() as td:
            rows=analyze.aggregate([rec]); analyze.write_outputs(rows,Path(td),[rec])
            self.assertTrue((Path(td)/"summary.csv").exists())
            self.assertTrue((Path(td)/"comparability.json").exists())
            self.assertIn("No Nexora",(Path(td)/"report.md").read_text())

    def test_mismatched_cross_platform_params_are_flagged(self):
        recs=[
            {"schema":"nexora.bench.v1","benchmark":"allocation_latency","platform":"linux","variant":"malloc","params":{"size_bytes":4096,"iterations":500,"warmup":50},"metrics":{"median_ns":10}},
            {"schema":"nexora.bench.v1","benchmark":"allocation_latency","platform":"nexora","variant":"ai_tensor_create_release","params":{"size_bytes":4096,"iterations":100,"warmup":10},"metrics":{"median_ns":8}},
        ]
        with tempfile.TemporaryDirectory() as td:
            analyze.write_outputs(analyze.aggregate(recs),Path(td),recs)
            report=(Path(td)/"report.md").read_text()
            self.assertIn("none have identical benchmark IDs and parameter objects",report)

    def test_matched_cross_platform_params_are_identified(self):
        params={"size_bytes":4096,"iterations":500,"warmup":50}
        recs=[
            {"schema":"nexora.bench.v1","benchmark":"allocation_latency","platform":"linux","variant":"malloc","params":params,"metrics":{"median_ns":10}},
            {"schema":"nexora.bench.v1","benchmark":"allocation_latency","platform":"nexora","variant":"ai_tensor_create_release","params":params,"metrics":{"median_ns":8}},
        ]
        audit=analyze.comparability_audit(recs)
        self.assertEqual(len(audit["matched_parameter_groups"]),1)

    def test_rejects_non_numeric_metric(self):
        rec={"schema":"nexora.bench.v1","benchmark":"b","platform":"linux","variant":"v","params":{},"metrics":{"x":"bad"}}
        with self.assertRaises(ValueError): analyze.aggregate([rec])

    def test_rejects_fixture_telemetry(self):
        rec={"schema":"nexora.bench.v1","benchmark":"b","platform":"nexora","variant":"v","params":{},"metrics":{"x":1},"fixture":True}
        with self.assertRaises(ValueError): analyze.aggregate([rec])

if __name__=="__main__": unittest.main()
