CC ?= gcc
PYTHON ?= python3
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -Itests/compat -Iinclude

HOST_TEST := build-phase16/test_phase16
PHASE16_SRC := src/ai/telemetry.c src/ai/fault_injection.c src/ai/release_health.c

.PHONY: test test-c test-python demo sanitize clean

test: test-c test-python

test-c: $(HOST_TEST)
	./$(HOST_TEST)

test-python:
	$(PYTHON) -m unittest -v tests/test_validator.py

$(HOST_TEST): tests/test_phase16.c $(PHASE16_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@

demo:
	@mkdir -p build-phase16/demo
	$(PYTHON) host/phase16_tracegen.py --out-dir build-phase16/demo
	$(PYTHON) host/phase16_validate.py \
		--baseline build-phase16/demo/baseline.csv \
		--candidate build-phase16/demo/candidate.csv \
		--json-out build-phase16/demo/report.json

sanitize:
	@mkdir -p build-phase16/sanitize
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-Itests/compat -Iinclude tests/test_phase16.c $(PHASE16_SRC) \
		-o build-phase16/sanitize/test_phase16
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 build-phase16/sanitize/test_phase16

clean:
	rm -rf build-phase16
