#!/usr/bin/env python3
"""Run the cocotb smoke test through the cocotb 2.x runner API."""

from pathlib import Path

from cocotb_tools.runner import get_runner

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BUILD = ROOT / "build" / "smoke" / "cocotb"

runner = get_runner("verilator")
runner.build(
    hdl_library="clover_smoke_lib",
    sources=[HERE / "clover_smoke.sv"],
    hdl_toplevel="clover_smoke",
    build_dir=BUILD,
    always=True,
    parameters={"WIDTH": 32},
    build_args=["-Wno-fatal"],
)
runner.test(
    hdl_toplevel="clover_smoke",
    hdl_toplevel_library="clover_smoke_lib",
    test_module="test_smoke",
    build_dir=BUILD,
    test_dir=HERE,
    seed=1,
)
print("cocotb smoke passed")
