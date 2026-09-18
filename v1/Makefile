# Everything, from the top.
#
#   make            run every check: firmware unit tests, hardware design
#                   rules, tool tests, and the C-vs-Python cross-check
#   make firmware   host build + unit tests only
#   make target     cross-compile for the STM32G474 and print the size report
#   make hardware   the design rules against board.yaml
#   make tools      the characterization tools' tests
#   make report     the three tables worth having on hand during bring-up
#   make clean

PYTHON ?= python3

.PHONY: all check firmware target hardware tools report clean

all: check

check: firmware hardware tools
	@echo
	@echo "================================================================"
	@echo " everything passed"
	@echo "================================================================"

firmware:
	@echo "=== firmware: host unit tests ==============================="
	@$(MAKE) -C firmware --no-print-directory test

target:
	@echo "=== firmware: cross build for STM32G474RET6 =================="
	@$(MAKE) -C firmware -f Makefile.target --no-print-directory

hardware:
	@echo "=== hardware: design rules against board.yaml ================"
	@cd hardware && $(PYTHON) -m pytest rules/ -q

tools:
	@echo "=== tools: characterization + C/Python cross-check ==========="
	@cd tools && $(PYTHON) -m pytest tests/ -q

# The three things you want printed out and taped to the bench before the
# first board arrives.
report:
	@echo "################ CAN bit timing ################"
	@$(PYTHON) tools/can_timing.py --table
	@echo
	@echo "################ PD compatibility ##############"
	@$(PYTHON) tools/pd_matrix.py --check-invariant
	@echo
	@echo "################ Power budget ##################"
	@$(PYTHON) tools/power_budget.py

clean:
	@$(MAKE) -C firmware --no-print-directory clean
	@$(MAKE) -C firmware -f Makefile.target --no-print-directory clean
	@rm -rf hardware/rules/__pycache__ tools/__pycache__ \
	        tools/tests/__pycache__ .pytest_cache \
	        hardware/.pytest_cache tools/.pytest_cache
