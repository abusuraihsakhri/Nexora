CC      ?= gcc
LD      ?= ld
QEMU    ?= qemu-system-x86_64

BUILD      := build
HOST_BUILD := build-host
ISO_DIR    := $(BUILD)/iso
KERNEL     := $(BUILD)/kernel.elf
ISO        := $(BUILD)/aikernel.iso
HOST_TEST  := $(HOST_BUILD)/phase14_tests
HOST_REPORT := $(HOST_BUILD)/phase14_report

CFLAGS  := -std=c11 -O2 -Wall -Wextra -Werror \
           -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
           -m64 -mno-red-zone \
           -Iinclude

HOST_CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -Iinclude -DHOST_TEST
ASFLAGS := -m64 -ffreestanding -fno-pic -fno-pie
LDFLAGS := -nostdlib -z max-page-size=0x1000 -T linker.ld

ARCH_C_SOURCES := \
	src/arch/x86_64/gdt.c \
	src/arch/x86_64/idt.c \
	src/arch/x86_64/syscall_msr.c

ARCH_ASM_SOURCES := \
	src/arch/x86_64/boot.S \
	src/arch/x86_64/interrupts.S \
	src/arch/x86_64/syscall_entry.S \
	src/arch/x86_64/usermode.S

SYS_SOURCES := \
	src/kernel/handle.c \
	src/kernel/process.c \
	src/kernel/uaccess.c \
	src/kernel/backend.c \
	src/kernel/elf64.c \
	src/kernel/syscall.c \
	src/kernel/demo_elf.c \
	src/ai/backend_bridge.c

AI_SOURCES := \
	src/ai/tensor.c \
	src/ai/work.c \
	src/ai/capability.c \
	src/ai/scheduler.c \
	src/ai/benchmark.c \
	src/ai/integration.c

C_SOURCES := \
	src/kernel/kmain.c \
	src/kernel/printk.c \
	src/kernel/panic.c \
	src/mm/bump.c \
	src/mm/frame.c \
	src/mm/slab.c \
	$(ARCH_C_SOURCES) \
	$(SYS_SOURCES) \
	$(AI_SOURCES)

ASM_SOURCES := $(ARCH_ASM_SOURCES)

OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(C_SOURCES)) \
           $(patsubst %.S,$(BUILD)/%.o,$(ASM_SOURCES))

HOST_TEST_SOURCES := \
	tests/test_phase14.c \
	tests/host_panic.c \
	src/mm/bump.c \
	src/mm/frame.c \
	src/mm/slab.c \
	$(ARCH_C_SOURCES) \
	$(SYS_SOURCES) \
	$(AI_SOURCES)

HOST_REPORT_SOURCES := \
	tests/phase14_report.c \
	tests/host_panic.c \
	src/mm/bump.c \
	src/mm/frame.c \
	src/mm/slab.c \
	$(ARCH_C_SOURCES) \
	$(SYS_SOURCES) \
	$(AI_SOURCES)

.PHONY: all clean run debug check test-host phase14-report verify-phase14

all: $(ISO)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL): $(OBJECTS) linker.ld
	@mkdir -p $(BUILD)
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@
	@echo "Built $(KERNEL)"

check: $(KERNEL)
	@command -v grub-file >/dev/null || \
		( echo "grub-file not found. Install grub-common/grub-pc-bin."; exit 1 )
	grub-file --is-x86-multiboot2 $(KERNEL)
	@echo "Multiboot2 header verified."

$(ISO): $(KERNEL) grub.cfg
	@command -v grub-mkrescue >/dev/null || \
		( echo "grub-mkrescue not found. Install grub-common/grub-pc-bin and xorriso."; exit 1 )
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/kernel.elf
	cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISO_DIR) >/dev/null 2>&1
	@echo "Built $(ISO)"

$(HOST_TEST): $(HOST_TEST_SOURCES)
	@mkdir -p $(HOST_BUILD)
	$(CC) $(HOST_CFLAGS) $(HOST_TEST_SOURCES) -o $@

# Runs the Phase 14 integration/stress suite without requiring GRUB or QEMU.
test-host: $(HOST_TEST)
	./$(HOST_TEST)

$(HOST_REPORT): $(HOST_REPORT_SOURCES)
	@mkdir -p $(HOST_BUILD)
	@$(CC) $(HOST_CFLAGS) $(HOST_REPORT_SOURCES) -o $@

phase14-report: $(HOST_REPORT)
	@./$(HOST_REPORT)

verify-phase14: test-host $(KERNEL)
	@echo "Phase 14 host tests and freestanding kernel link passed."

run: $(ISO)
	$(QEMU) \
		-m 512M \
		-smp 1 \
		-cdrom $(ISO) \
		-serial stdio \
		-display gtk \
		-no-reboot \
		-no-shutdown

debug: $(ISO)
	$(QEMU) \
		-m 512M \
		-smp 1 \
		-cdrom $(ISO) \
		-serial stdio \
		-display gtk \
		-no-reboot \
		-no-shutdown \
		-S -s

clean:
	rm -rf $(BUILD) $(HOST_BUILD)
