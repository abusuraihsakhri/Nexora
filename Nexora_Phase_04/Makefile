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
	src/mm/bump.c \
	src/ai/backing.c \
	src/ai/tensor.c \
	src/ai/mapping.c \
	src/ai/domain.c \
	src/ai/handle.c \
	src/ai/work.c \
	src/ai/capability.c \
	src/ai/scheduler.c

ASM_SOURCES := src/arch/x86_64/boot.S

OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(C_SOURCES)) \
           $(patsubst %.S,$(BUILD)/%.o,$(ASM_SOURCES))

.PHONY: all clean run debug check test-host test-host-sanitize test-host-concurrency test-host-tsan test-host-step9 test-host-step9-sanitize test-host-step9-tsan test-host-step9-stress test-host-step10 test-host-step10-sanitize benchmark-phase4 phase4-gate

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


test-host:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		tests/handle_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/handle_host_test
	$(BUILD)/tests/handle_host_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		tests/delegation_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/delegation_host_test
	$(BUILD)/tests/delegation_host_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		tests/mapping_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/mapping_host_test
	$(BUILD)/tests/mapping_host_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		tests/lifetime_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/lifetime_host_test
	$(BUILD)/tests/lifetime_host_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -pthread -Iinclude \
		tests/concurrency_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/concurrency_host_test
	$(BUILD)/tests/concurrency_host_test
	$(MAKE) test-host-step9
	$(MAKE) test-host-step10
	@echo "Host handle/delegation/mapping/lifetime/concurrency/security/integration tests passed."


test-host-sanitize:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Iinclude \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		tests/mapping_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/mapping_host_test_san
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/tests/mapping_host_test_san
	$(HOST_CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Iinclude \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		tests/lifetime_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/lifetime_host_test_san
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/tests/lifetime_host_test_san
	$(HOST_CC) -std=c11 -O1 -g -Wall -Wextra -Werror -pthread -Iinclude \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		tests/concurrency_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/concurrency_host_test_san
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/tests/concurrency_host_test_san


test-host-concurrency:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -pthread -Iinclude \
		tests/concurrency_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/concurrency_host_test
	$(BUILD)/tests/concurrency_host_test


test-host-tsan:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O1 -g -Wall -Wextra -Werror -pthread -Iinclude \
		-fsanitize=thread -fno-omit-frame-pointer \
		tests/concurrency_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/concurrency_host_test_tsan
	TSAN_OPTIONS=halt_on_error=1 $(BUILD)/tests/concurrency_host_test_tsan


test-host-step9:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		tests/security_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/security_host_test
	$(BUILD)/tests/security_host_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -pthread -Iinclude \
		tests/race_matrix_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/race_matrix_host_test
	$(BUILD)/tests/race_matrix_host_test
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -pthread -Iinclude \
		tests/randomized_stress_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/randomized_stress_host_test
	$(BUILD)/tests/randomized_stress_host_test
	@echo "Step 9 adversarial/race/randomized tests passed."


test-host-step9-sanitize:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Iinclude \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		tests/security_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/security_host_test_san
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/tests/security_host_test_san
	$(HOST_CC) -std=c11 -O1 -g -Wall -Wextra -Werror -pthread -Iinclude \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		tests/race_matrix_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/race_matrix_host_test_san
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/tests/race_matrix_host_test_san
	$(HOST_CC) -std=c11 -O1 -g -Wall -Wextra -Werror -pthread -Iinclude \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		tests/randomized_stress_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/randomized_stress_host_test_san
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/tests/randomized_stress_host_test_san


test-host-step9-tsan:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O1 -g -Wall -Wextra -Werror -pthread -Iinclude \
		-fsanitize=thread -fno-omit-frame-pointer \
		tests/race_matrix_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/race_matrix_host_test_tsan
	TSAN_OPTIONS=halt_on_error=1 $(BUILD)/tests/race_matrix_host_test_tsan
	$(HOST_CC) -std=c11 -O1 -g -Wall -Wextra -Werror -pthread -Iinclude \
		-fsanitize=thread -fno-omit-frame-pointer \
		tests/randomized_stress_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/tests/randomized_stress_host_test_tsan
	TSAN_OPTIONS=halt_on_error=1 $(BUILD)/tests/randomized_stress_host_test_tsan


test-host-step9-stress: test-host-step9
	@i=0; while [ $$i -lt 10 ]; do \
		$(BUILD)/tests/race_matrix_host_test >/dev/null || exit 1; \
		$(BUILD)/tests/randomized_stress_host_test >/dev/null || exit 1; \
		i=$$((i + 1)); \
	done
	@echo "Step 9 repeated stress: 10/10 PASS"


test-host-step10:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		tests/phase4_integration_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c src/ai/work.c src/ai/scheduler.c \
		-o $(BUILD)/tests/phase4_integration_host_test
	$(BUILD)/tests/phase4_integration_host_test
	@echo "Step 10 Phase 4 integration gate passed."


test-host-step10-sanitize:
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Iinclude \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		tests/phase4_integration_host_test.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c src/ai/work.c src/ai/scheduler.c \
		-o $(BUILD)/tests/phase4_integration_host_test_san
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/tests/phase4_integration_host_test_san


benchmark-phase4:
	@mkdir -p $(BUILD)/benchmarks
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		benchmarks/zero_copy_benchmark.c src/ai/domain.c src/ai/handle.c src/ai/mapping.c src/ai/backing.c src/ai/tensor.c \
		-o $(BUILD)/benchmarks/zero_copy_benchmark
	$(BUILD)/benchmarks/zero_copy_benchmark


phase4-gate: $(KERNEL) test-host test-host-sanitize test-host-step9-sanitize test-host-step10-sanitize benchmark-phase4
	@echo "Phase 4 integration gate: PASS"
