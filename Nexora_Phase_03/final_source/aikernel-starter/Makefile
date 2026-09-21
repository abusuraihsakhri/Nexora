CC      ?= gcc
HOST_CC ?= gcc
LD      ?= ld
QEMU    ?= qemu-system-x86_64

BUILD   := build
ISO_DIR := $(BUILD)/iso
KERNEL  := $(BUILD)/kernel.elf
ISO     := $(BUILD)/aikernel.iso

CFLAGS  := -std=c11 -O2 -Wall -Wextra -Werror \
           -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
           -m64 -mno-red-zone \
           -Iinclude

ASFLAGS := -m64 -ffreestanding -fno-pic -fno-pie

LDFLAGS := -nostdlib -z max-page-size=0x1000 -T linker.ld

C_SOURCES := \
	src/kernel/kmain.c \
	src/kernel/printk.c \
	src/kernel/panic.c \
	src/kernel/object.c \
	src/mm/bump.c \
	src/mm/memory_object.c \
	src/ai/tensor.c \
	src/ai/device.c \
	src/ai/lifetime.c \
	src/ai/reclaim.c \
	src/ai/instrument.c \
	src/ai/debug.c \
	src/ai/work.c \
	src/ai/capability.c \
	src/ai/scheduler.c

ASM_SOURCES := src/arch/x86_64/boot.S

OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(C_SOURCES)) \
           $(patsubst %.S,$(BUILD)/%.o,$(ASM_SOURCES))

.PHONY: all clean run debug check test test-tensor test-backing test-ownership test-lifetime test-graph test-work test-placement test-reclaim test-instrument

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

run: $(ISO)
	$(QEMU) \
		-m 512M \
		-smp 2 \
		-cdrom $(ISO) \
		-serial stdio \
		-display gtk \
		-no-reboot \
		-no-shutdown

debug: $(ISO)
	$(QEMU) \
		-m 512M \
		-smp 2 \
		-cdrom $(ISO) \
		-serial stdio \
		-display gtk \
		-no-reboot \
		-no-shutdown \
		-S -s

HOST_TEST_BIN := $(BUILD)/tests/tensor_metadata_test
HOST_BACKING_TEST_BIN := $(BUILD)/tests/tensor_backing_test
HOST_OWNERSHIP_TEST_BIN := $(BUILD)/tests/ownership_refcount_test
HOST_LIFETIME_TEST_BIN := $(BUILD)/tests/tensor_lifetime_test
HOST_GRAPH_TEST_BIN := $(BUILD)/tests/producer_consumer_graph_test
HOST_WORK_TEST_BIN := $(BUILD)/tests/work_object_redesign_test
HOST_PLACEMENT_TEST_BIN := $(BUILD)/tests/locality_placement_test
HOST_RECLAIM_TEST_BIN := $(BUILD)/tests/automatic_reclamation_test
HOST_INSTRUMENT_TEST_BIN := $(BUILD)/tests/phase3_instrumentation_demo_test

test: test-tensor test-backing test-ownership test-lifetime test-graph test-work test-placement test-reclaim test-instrument

test-tensor:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		tests/tensor_metadata_test.c src/ai/tensor.c src/ai/device.c src/ai/lifetime.c src/ai/instrument.c src/mm/memory_object.c src/kernel/object.c \
		-o $(HOST_TEST_BIN)
	$(HOST_TEST_BIN)

test-backing:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		tests/tensor_backing_test.c src/ai/tensor.c src/ai/device.c src/ai/lifetime.c src/ai/instrument.c src/mm/memory_object.c src/kernel/object.c \
		-o $(HOST_BACKING_TEST_BIN)
	$(HOST_BACKING_TEST_BIN)


test-ownership:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		tests/ownership_refcount_test.c src/ai/tensor.c src/ai/device.c src/ai/lifetime.c src/ai/instrument.c src/mm/memory_object.c src/kernel/object.c \
		-o $(HOST_OWNERSHIP_TEST_BIN)
	$(HOST_OWNERSHIP_TEST_BIN)

clean:
	rm -rf $(BUILD)


test-lifetime:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		tests/tensor_lifetime_test.c src/ai/tensor.c src/ai/device.c src/ai/lifetime.c src/ai/instrument.c src/mm/memory_object.c src/kernel/object.c \
		-o $(HOST_LIFETIME_TEST_BIN)
	$(HOST_LIFETIME_TEST_BIN)

test-graph:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		tests/producer_consumer_graph_test.c src/ai/tensor.c src/ai/device.c src/ai/lifetime.c src/ai/reclaim.c src/ai/instrument.c src/ai/work.c src/ai/scheduler.c \
		src/mm/memory_object.c src/kernel/object.c \
		-o $(HOST_GRAPH_TEST_BIN)
	$(HOST_GRAPH_TEST_BIN)


test-work:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		tests/work_object_redesign_test.c src/ai/tensor.c src/ai/device.c src/ai/lifetime.c src/ai/reclaim.c src/ai/instrument.c src/ai/work.c src/ai/scheduler.c \
		src/mm/memory_object.c src/kernel/object.c \
		-o $(HOST_WORK_TEST_BIN)
	$(HOST_WORK_TEST_BIN)


test-placement:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		tests/locality_placement_test.c src/ai/tensor.c src/ai/device.c src/ai/lifetime.c src/ai/reclaim.c src/ai/instrument.c src/ai/work.c src/ai/scheduler.c \
		src/mm/memory_object.c src/kernel/object.c \
		-o $(HOST_PLACEMENT_TEST_BIN)
	$(HOST_PLACEMENT_TEST_BIN)


test-reclaim:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		tests/automatic_reclamation_test.c src/ai/tensor.c src/ai/device.c src/ai/lifetime.c src/ai/reclaim.c src/ai/instrument.c src/ai/work.c src/ai/scheduler.c \
		src/mm/memory_object.c src/kernel/object.c \
		-o $(HOST_RECLAIM_TEST_BIN)
	$(HOST_RECLAIM_TEST_BIN)


test-instrument:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		tests/phase3_instrumentation_demo_test.c src/ai/tensor.c src/ai/device.c src/ai/lifetime.c src/ai/reclaim.c src/ai/instrument.c src/ai/work.c src/ai/scheduler.c \
		src/mm/memory_object.c src/kernel/object.c \
		-o $(HOST_INSTRUMENT_TEST_BIN)
	$(HOST_INSTRUMENT_TEST_BIN)
