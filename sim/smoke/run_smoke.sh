#!/usr/bin/env bash
# L0 toolchain smoke: Verilator lint + Icarus run + cocotb + Yosys check.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="$ROOT/build/smoke"
PY="$ROOT/.venv/bin/python"

mkdir -p "$BUILD"

echo "== Verilator lint =="
verilator --lint-only -Wall -Wno-fatal \
    --top-module clover_smoke \
    "$ROOT/sim/smoke/clover_smoke.sv"

echo "== Icarus compile + run =="
iverilog -g2012 -o "$BUILD/tb_smoke.vvp" \
    "$ROOT/sim/smoke/tb_smoke.sv" \
    "$ROOT/sim/smoke/clover_smoke.sv"
vvp "$BUILD/tb_smoke.vvp" | tee "$BUILD/icarus.log"
grep -q "SMOKE PASS" "$BUILD/icarus.log"

echo "== cocotb =="
"$PY" "$ROOT/sim/smoke/run_cocotb.py"

echo "== Yosys read/proc/check =="
yosys -q -p "
    read_verilog -sv $ROOT/sim/smoke/clover_smoke.sv
    hierarchy -check -top clover_smoke
    proc
    opt
    check -assert
    stat
" | tee "$BUILD/yosys.log"

echo "== ALL SMOKE TESTS PASSED =="
