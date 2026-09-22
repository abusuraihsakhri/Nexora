.PHONY: verify test kernel bench qualification release qemu clean

verify: kernel bench qualification release
	@echo "Nexora verification completed."

test: verify

kernel:
	$(MAKE) -C kernel verify-current

bench:
	$(MAKE) -C bench test

qualification:
	$(MAKE) -C qualification test
	$(MAKE) -C qualification demo

release:
	$(MAKE) -C release gate

qemu:
	$(MAKE) -C kernel all
	@set -eu; \
	log=$$(mktemp); \
	set +e; \
	timeout 12s qemu-system-x86_64 -m 512M -smp 1 \
	  -cdrom kernel/build/aikernel.iso -serial stdio -display none \
	  -no-reboot -no-shutdown >$$log 2>&1; \
	rc=$$?; set -e; cat $$log; \
	if [ $$rc -ne 0 ] && [ $$rc -ne 124 ]; then rm -f $$log; exit $$rc; fi; \
	grep -F "Nexora initialization complete." $$log; \
	rm -f $$log

clean:
	$(MAKE) -C kernel clean
	$(MAKE) -C bench clean
	$(MAKE) -C qualification clean
	$(MAKE) -C release clean
