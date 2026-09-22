.PHONY: test fixture summarize clean

test:
	sh ./scripts/selftest.sh

fixture:
	python3 tools/generate_fixture.py

summarize: fixture
	python3 tools/nxbench.py summarize fixtures/sample_run.jsonl --output results/sample_summary.json

clean:
	rm -f results/*.json results/*.o fixtures/sample_run.jsonl fixtures/baseline_summary.json
