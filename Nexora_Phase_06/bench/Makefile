CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -D_GNU_SOURCE
LDFLAGS ?=

SRC := src
BIN := bin
COMMON := $(SRC)/common.c
PROGRAMS := alloc_latency graph_sched lifetime_peak zero_copy_ipc deadline_tail
TARGETS := $(addprefix $(BIN)/,$(PROGRAMS))

.PHONY: all clean

all: $(TARGETS)

$(BIN):
	mkdir -p $(BIN)

$(BIN)/%: $(SRC)/%.c $(COMMON) $(SRC)/common.h | $(BIN)
	$(CC) $(CFLAGS) $< $(COMMON) -o $@ $(LDFLAGS)

clean:
	rm -rf $(BIN)
