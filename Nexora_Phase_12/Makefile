CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -Iinclude

BUILD := build
CORE := src/ai/reliability.c
TESTS := $(BUILD)/test_phase12 $(BUILD)/test_policy_edges

.PHONY: all test freestanding clean

all: test

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_phase12: $(CORE) tests/test_phase12.c | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@

$(BUILD)/test_policy_edges: $(CORE) tests/test_policy_edges.c | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@

# Compile the kernel subsystem as freestanding code; no link step is required.
freestanding: | $(BUILD)
	$(CC) -std=c11 -ffreestanding -fno-builtin -Wall -Wextra -Wpedantic -Werror \
		$(CPPFLAGS) -c $(CORE) -o $(BUILD)/reliability.o

test: $(TESTS) freestanding
	./$(BUILD)/test_phase12
	./$(BUILD)/test_policy_edges

clean:
	rm -rf $(BUILD)
