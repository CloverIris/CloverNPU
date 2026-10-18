#!/usr/bin/env python3
"""Host -> NPU RUN kernel end-to-end test."""

from __future__ import annotations

import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from cocotb_tools.runner import get_runner

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "cmake" / "bin"
WORK = ROOT / "build" / "npu_core"
SIM_BUILD = WORK / "sim"


def main() -> int:
    if not (BIN / "cloverc").exists():
        print("missing build/cmake/bin; run `make tools` first", file=sys.stderr)
        return 1
    WORK.mkdir(parents=True, exist_ok=True)
    if SIM_BUILD.exists():
        shutil.rmtree(SIM_BUILD)

    kernel = WORK / "kernel_sum.bin"
    compiled = subprocess.run(
        [
            str(BIN / "cloverc"),
            str(ROOT / "tests" / "npu" / "kernel_sum.c"),
            "--no-start",
            "--base",
            "0",
            "-o",
            str(kernel),
        ],
        text=True,
        capture_output=True,
    )
    if compiled.returncode != 0:
        print(f"kernel compile failed:\n{compiled.stderr}")
        return 1

    runner = get_runner("verilator")
    runner.build(
        hdl_library="npu_core_lib",
        sources=[ROOT / "sim" / "npu" / "clover_npu_mem_tb.sv"],
        hdl_toplevel="clover_npu_mem_tb",
        build_dir=SIM_BUILD,
        includes=[
            ROOT / "rtl" / "common",
            ROOT / "rtl" / "top",
            ROOT / "rtl" / "bus",
            ROOT / "rtl" / "cmd",
            ROOT / "rtl" / "dma",
            ROOT / "rtl" / "mem",
            ROOT / "rtl" / "core",
            ROOT / "rtl" / "fp",
            ROOT / "rtl" / "tile",
            ROOT / "sim" / "dma",
            ROOT / "sim" / "npu",
        ],
        build_args=["--timing", "-Wno-fatal"],
        always=True,
    )
    results = runner.test(
        hdl_toplevel="clover_npu_mem_tb",
        hdl_toplevel_library="npu_core_lib",
        test_module="test_npu_core",
        test_dir=ROOT / "sim" / "npu",
        build_dir=SIM_BUILD,
        plusargs=[f"+KERNEL_BIN={kernel}"],
        seed=1,
    )
    tree = ET.parse(results)
    problems = tree.findall(".//failure") + tree.findall(".//error")
    if problems:
        print(f"FAIL npu_run_kernel: {problems[0].get('message', 'test failed')}")
        return 1
    print("PASS npu_run_kernel")
    return 0


if __name__ == "__main__":
    sys.exit(main())
