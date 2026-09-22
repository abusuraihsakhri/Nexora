.PHONY: verify kernel phase15 phase16 phase17 docs clean

verify: kernel phase15 phase16 phase17 docs
	@echo "Nexora repository verification: PASS"

kernel:
	$(MAKE) -C Nexora_Phase_14 verify-phase14

phase15:
	$(MAKE) -C Nexora_Phase_15 test

phase16:
	$(MAKE) -C Nexora_Phase_16 -f Makefile.phase16 test

phase17:
	cd Nexora_Phase_17 && python3 tools/release_gate.py

docs:
	python3 tools/validate_docs.py

clean:
	$(MAKE) -C Nexora_Phase_14 clean
	$(MAKE) -C Nexora_Phase_15 clean
	$(MAKE) -C Nexora_Phase_16 -f Makefile.phase16 clean
	$(MAKE) -C Nexora_Phase_17 clean
