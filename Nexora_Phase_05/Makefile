CC ?= gcc
LD ?= ld
BUILD := build
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -pedantic -Iinclude
FREESTANDING_CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -Iinclude
FREESTANDING_ASFLAGS := -m64 -ffreestanding -fno-pic -fno-pie -Iinclude

HOST_SOURCES := \
	kernel/handle.c \
	kernel/process.c \
	kernel/uaccess.c \
	kernel/backend.c \
	kernel/elf64.c \
	kernel/syscall.c \
	tests/mock_backend.c \
	tests/test_syscall.c

.PHONY: all cross-check test arch-check user-check check clean

all: check

cross-check:
	python3 scripts/cross_check_phase5.py
	$(CC) -std=c11 -Wall -Wextra -Werror -Wno-enum-compare -Ireference/starter_include -Iinclude -fsyntax-only reference/compat_check.c
	@echo "Starter enum/value compatibility: PASS (known output-limit delta documented)"

test: $(BUILD)/test_phase5
	./$(BUILD)/test_phase5

$(BUILD)/test_phase5: $(HOST_SOURCES)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(HOST_SOURCES) -o $@

arch-check:
	@mkdir -p $(BUILD)/arch
	$(CC) $(FREESTANDING_CFLAGS) -c arch/x86_64/gdt.c -o $(BUILD)/arch/gdt.o
	$(CC) $(FREESTANDING_CFLAGS) -c arch/x86_64/syscall_msr.c -o $(BUILD)/arch/syscall_msr.o
	$(CC) $(FREESTANDING_ASFLAGS) -c arch/x86_64/syscall_entry.S -o $(BUILD)/arch/syscall_entry.o
	$(CC) $(FREESTANDING_ASFLAGS) -c arch/x86_64/usermode.S -o $(BUILD)/arch/usermode.o
	$(CC) $(FREESTANDING_CFLAGS) -c kernel/handle.c -o $(BUILD)/arch/handle.o
	$(CC) $(FREESTANDING_CFLAGS) -c kernel/process.c -o $(BUILD)/arch/process.o
	$(CC) $(FREESTANDING_CFLAGS) -c kernel/uaccess.c -o $(BUILD)/arch/uaccess.o
	$(CC) $(FREESTANDING_CFLAGS) -c kernel/backend.c -o $(BUILD)/arch/backend.o
	$(CC) $(FREESTANDING_CFLAGS) -c kernel/elf64.c -o $(BUILD)/arch/elf64.o
	$(CC) $(FREESTANDING_CFLAGS) -c kernel/syscall.c -o $(BUILD)/arch/syscall.o

user-check:
	@mkdir -p $(BUILD)/user
	$(CC) $(FREESTANDING_CFLAGS) -Iuser/libnexora -c user/libnexora/nexora.c -o $(BUILD)/user/libnexora.o
	$(CC) $(FREESTANDING_CFLAGS) -Iuser/libnexora -c user/demo/main.c -o $(BUILD)/user/main.o
	$(CC) $(FREESTANDING_ASFLAGS) -c user/demo/start.S -o $(BUILD)/user/start.o
	$(LD) -nostdlib -T user/user.ld $(BUILD)/user/start.o $(BUILD)/user/main.o $(BUILD)/user/libnexora.o -o $(BUILD)/user/demo.elf

check: cross-check test arch-check user-check
	@echo "Phase 5 validation complete."

clean:
	rm -rf $(BUILD)
