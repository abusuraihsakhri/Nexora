CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -Iinclude
FREESTANDING_CFLAGS := -std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector -Wall -Wextra -Wpedantic -Werror
SAN_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer

BUILD := build
REL := src/ai/reliability.c
OBS := src/ai/observability.c
OBS_REL := src/ai/obs_reliability.c
CORE := $(REL) $(OBS) $(OBS_REL)
TESTS := $(BUILD)/test_phase12 $(BUILD)/test_policy_edges $(BUILD)/test_phase13 $(BUILD)/test_phase13_edges
SAN_TESTS := $(BUILD)/san_phase12 $(BUILD)/san_phase13 $(BUILD)/san_phase13_edges

.PHONY: all test freestanding symbols sanitizers audit validate clean

all: test

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_phase12: $(REL) tests/test_phase12.c | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@

$(BUILD)/test_policy_edges: $(REL) tests/test_policy_edges.c | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@

$(BUILD)/test_phase13: $(CORE) tests/test_phase13.c | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@

$(BUILD)/test_phase13_edges: $(CORE) tests/test_phase13_edges.c | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@

freestanding: | $(BUILD)
	$(CC) $(FREESTANDING_CFLAGS) $(CPPFLAGS) -c $(REL) -o $(BUILD)/reliability.o
	$(CC) $(FREESTANDING_CFLAGS) $(CPPFLAGS) -c $(OBS) -o $(BUILD)/observability.o
	$(CC) $(FREESTANDING_CFLAGS) $(CPPFLAGS) -c $(OBS_REL) -o $(BUILD)/obs_reliability.o

symbols: freestanding
	$(CC) -nostdlib -r $(BUILD)/reliability.o $(BUILD)/observability.o $(BUILD)/obs_reliability.o -o $(BUILD)/nexora_phase13_core.o
	@test -z "$$(nm -u $(BUILD)/nexora_phase13_core.o)" || (nm -u $(BUILD)/nexora_phase13_core.o; exit 1)

$(BUILD)/san_phase12: $(REL) tests/test_phase12.c | $(BUILD)
	$(CC) $(SAN_CFLAGS) $(CPPFLAGS) $^ -o $@

$(BUILD)/san_phase13: $(CORE) tests/test_phase13.c | $(BUILD)
	$(CC) $(SAN_CFLAGS) $(CPPFLAGS) $^ -o $@

$(BUILD)/san_phase13_edges: $(CORE) tests/test_phase13_edges.c | $(BUILD)
	$(CC) $(SAN_CFLAGS) $(CPPFLAGS) $^ -o $@

sanitizers: $(SAN_TESTS)
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD)/san_phase12
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD)/san_phase13
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD)/san_phase13_edges

test: $(TESTS) freestanding
	./$(BUILD)/test_phase12
	./$(BUILD)/test_policy_edges
	./$(BUILD)/test_phase13
	./$(BUILD)/test_phase13_edges

audit:
	python3 tools/phase13_audit.py

validate: test sanitizers symbols audit
	@echo "Phase 13 validation: PASS"

clean:
	rm -rf $(BUILD)
