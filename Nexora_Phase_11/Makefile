CC      ?= gcc
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
	src/mm/bump.c \
	src/ai/tensor.c \
	src/ai/work.c \
	src/ai/capability.c \
	src/ai/policy.c \
	src/ai/policy_demo.c \
	src/ai/scheduler.c

ASM_SOURCES := src/arch/x86_64/boot.S

OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(C_SOURCES)) \
           $(patsubst %.S,$(BUILD)/%.o,$(ASM_SOURCES))

.PHONY: all clean run debug check phase11-test phase11-sanitize phase11-analyze phase11-crosscheck

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

clean:
	rm -rf $(BUILD)


phase11-test:
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		tests/phase11_policy_test.c src/ai/policy.c -o $(BUILD)/phase11_policy_test
	$(BUILD)/phase11_policy_test
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		tests/phase11_property_test.c src/ai/policy.c -o $(BUILD)/phase11_property_test
	$(BUILD)/phase11_property_test


phase11-sanitize:
	@mkdir -p $(BUILD)/san
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude \
		tests/phase11_policy_test.c src/ai/policy.c -o $(BUILD)/san/phase11_policy_test
	$(BUILD)/san/phase11_policy_test
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude \
		tests/phase11_property_test.c src/ai/policy.c -o $(BUILD)/san/phase11_property_test
	$(BUILD)/san/phase11_property_test

phase11-analyze:
	@mkdir -p $(BUILD)/analyze
	$(CC) -std=c11 -O0 -Wall -Wextra -Werror -fanalyzer -Iinclude \
		-c src/ai/policy.c -o $(BUILD)/analyze/policy.o

phase11-crosscheck:
	./scripts/phase11_crosscheck.sh
