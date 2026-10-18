SHELL   := /bin/bash
ROOT    := $(CURDIR)
PY      := $(ROOT)/.venv/bin/python
BUILD   := $(ROOT)/build
CMAKE_BUILD := $(BUILD)/cmake
CC      ?= cc
CXX     ?= c++

.PHONY: help all check check-tools abi-check tools compiler m2-test rtl-lint rtl-synth rtl-core-test smoke lint test synth clean venv

help:
	@echo "CloverNPU targets:"
	@echo "  make check-tools  print tool versions / verify installation"
	@echo "  make abi-check    compile-time ABI layout assertions"
	@echo "  make tools        build cloverc/clover-as/clover-iss/clover-objdump"
	@echo "  make m2-test      run assembler/ISS/compiler vertical-slice tests"
	@echo "  make rtl-lint     Verilator lint of the Clover-S core"
	@echo "  make rtl-synth    Yosys read/proc/check of the Clover-S core"
	@echo "  make rtl-core-test run the C programs on the RTL core"
	@echo "  make smoke        L0 toolchain smoke (Verilator/Icarus/cocotb/Yosys)"
	@echo "  make lint         Verilator lint of the smoke DUT"
	@echo "  make test         run cocotb smoke test"
	@echo "  make synth        Yosys read/proc/check of the smoke DUT"
	@echo "  make check        all of the above"
	@echo "  make clean        remove build artifacts"

all: check

check: check-tools abi-check smoke m2-test rtl-lint rtl-synth rtl-core-test

check-tools:
	@./scripts/check_tools.sh

tools:
	cmake -S "$(ROOT)" -B "$(CMAKE_BUILD)" -G Ninja -DCMAKE_BUILD_TYPE=Release
	cmake --build "$(CMAKE_BUILD)"

compiler: tools

m2-test: tools
	$(PY) tests/m2/run_tests.py

rtl-lint:
	verilator --lint-only -Wall -Wno-fatal \
		--top-module clover_core \
		-Irtl/common -Irtl/core rtl/core/clover_core.sv

rtl-synth:
	yosys -q -p "\
		read_verilog -sv -Irtl/common -Irtl/core rtl/core/clover_core.sv; \
		hierarchy -check -top clover_core; \
		proc; opt; check -assert; stat"

rtl-core-test: tools
	$(PY) tests/rtl/run_core_tests.py

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
	rm -rf "$(BUILD)/smoke" "$(BUILD)/abi" "$(CMAKE_BUILD)" "$(BUILD)/m2" "$(BUILD)/rtl"
	rm -rf "$(BUILD)/host"
	rm -rf "$(ROOT)/sim/smoke/__pycache__"
	find "$(ROOT)/sim" -name '*.vvp' -delete
	rm -f "$(ROOT)/sim/smoke/results.xml" "$(ROOT)/sim/rtl/results.xml"
	@echo "cleaned build/smoke, build/abi and sim artifacts"
