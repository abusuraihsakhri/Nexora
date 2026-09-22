.PHONY: all test gate manifest verify clean
all: test
test:
	./tests/run_tests.sh
gate:
	python3 tools/release_gate.py
manifest:
	python3 tools/make_manifest.py .
verify:
	python3 tools/verify_manifest.py PHASE17_MANIFEST.json
clean:
	rm -rf build PHASE17_MANIFEST.json
