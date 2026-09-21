.PHONY: all clean test quick

all:
	$(MAKE) -C bench all

clean:
	$(MAKE) -C bench clean
	rm -rf results/quick results/test-run

test: all
	python3 -m unittest discover -s tests -p 'test_*.py' -v
	bash tests/smoke.sh

quick: all
	python3 scripts/run_suite.py --quick --repetitions 3 --output results/quick
	python3 scripts/analyze.py results/quick/raw.jsonl --out-dir results/quick/analysis
