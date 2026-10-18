SHELL   := /bin/bash
ROOT    := $(CURDIR)
PY      := $(ROOT)/.venv/bin/python
BUILD   := $(ROOT)/build
CC      ?= cc
CXX     ?= c++

.PHONY: help all check check-tools abi-check smoke lint test synth clean venv

help:
	@echo "CloverNPU targets:"
	@echo "  make check-tools  print tool versions / verify installation"
	@echo "  make abi-check    compile-time ABI layout assertions"
	@echo "  make smoke        L0 toolchain smoke (Verilator/Icarus/cocotb/Yosys)"
	@echo "  make lint         Verilator lint of the smoke DUT"
	@echo "  make test         run cocotb smoke test"
	@echo "  make synth        Yosys read/proc/check of the smoke DUT"
	@echo "  make check        check-tools + abi-check + smoke"
	@echo "  make clean        remove build artifacts"

all: check

check: check-tools abi-check smoke

check-tools:
	@./scripts/check_tools.sh

abi-check:
	@mkdir -p "$(BUILD)/abi"
	$(CXX) -std=c++17 -Wall -Wextra -Iinclude \
		tests/abi/test_abi_layout.cpp -o "$(BUILD)/abi/test_abi_layout"
	"$(BUILD)/abi/test_abi_layout"
	$(CC) -std=c11 -Wall -Wextra -Iinclude -c \
		tests/abi/test_headers.c -o "$(BUILD)/abi/test_headers.o"

smoke:
	@./sim/smoke/run_smoke.sh

lint:
	verilator --lint-only -Wall -Wno-fatal \
		--top-module clover_smoke \
		sim/smoke/clover_smoke.sv

test:
	$(PY) sim/smoke/run_cocotb.py

synth:
	yosys -q -p "\
		read_verilog -sv sim/smoke/clover_smoke.sv; \
		hierarchy -check -top clover_smoke; \
		proc; opt; check -assert; stat"

venv:
	python3 -m venv .venv
	.venv/bin/python -m pip install -q -r requirements.txt

clean:
	rm -rf "$(BUILD)/smoke" "$(BUILD)/abi"
	rm -rf "$(ROOT)/sim/smoke/__pycache__"
	find "$(ROOT)/sim" -name '*.vvp' -delete
	rm -f "$(ROOT)/sim/smoke/results.xml"
	@echo "cleaned build/smoke, build/abi and sim artifacts"
