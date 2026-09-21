CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -pedantic
CPPFLAGS ?= -Iinclude

SRC := $(wildcard kernel/phase10/*.c)
TEST := tests/test_phase10.c
ADV_TEST := tests/test_phase10_adversarial.c
BIN := build/test_phase10
ADV_BIN := build/test_phase10_adversarial

.PHONY: all test test-adversarial crosscheck clean freestanding-check

all: $(BIN) $(ADV_BIN)

$(BIN): $(SRC) $(TEST)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) $(TEST) -o $@

$(ADV_BIN): $(SRC) $(ADV_TEST)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) $(ADV_TEST) -o $@

test: $(BIN)
	./$(BIN)

test-adversarial: $(ADV_BIN)
	./$(ADV_BIN)

freestanding-check:
	@mkdir -p build/freestanding
	@for f in $(SRC); do \
	  b=build/freestanding/$$(basename $$f .c).o; \
	  $(CC) $(CPPFLAGS) $(CFLAGS) -ffreestanding -fno-builtin -c $$f -o $$b; \
	done
	@echo "Freestanding compile check: PASS"

crosscheck: test test-adversarial freestanding-check
	@echo "Phase 10 cross-check: PASS"

clean:
	rm -rf build
