#!/usr/bin/env python3
"""Full RTL GEMM through CP, DMA, core tile.mma and completion ring."""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from cocotb_tools.runner import get_runner

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "cmake" / "bin"
WORK = ROOT / "build" / "rtl_gemm"
SIM_BUILD = WORK / "sim"


def main() -> int:
    if not (BIN / "cloverc").exists():
        print("missing build/cmake/bin; run `make tools` first", file=sys.stderr)
        return 1
    WORK.mkdir(parents=True, exist_ok=True)
    if SIM_BUILD.exists():
        shutil.rmtree(SIM_BUILD)

    kernel = WORK / "gemm.bin"
    map_path = WORK / "gemm.map"
    compiled = subprocess.run(
        [
            str(BIN / "cloverc"),
            str(ROOT / "kernels" / "gemm.c"),
            "--no-start",
            "--base",
            "0",
            "--entry",
            "gemm",
            "--map",
            str(map_path),
            "-o",
            str(kernel),
        ],
        text=True,
        capture_output=True,
    )
    if compiled.returncode != 0:
        print(f"kernel compile failed:\n{compiled.stderr}")
        return 1

    entry = None
    for line in map_path.read_text().splitlines():
        match = re.match(r"0x([0-9a-fA-F]+)\s+gemm$", line.strip())
        if match:
            entry = int(match.group(1), 16)
            break
    if entry is None:
        print("gemm entry symbol not found")
        return 1

    runner = get_runner("verilator")
    runner.build(
        hdl_library="npu_gemm_lib",
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
    for shape in ((16, 16, 16), (17, 19, 23)):
        results = runner.test(
            hdl_toplevel="clover_npu_mem_tb",
            hdl_toplevel_library="npu_gemm_lib",
            test_module="test_npu_gemm",
            test_dir=ROOT / "sim" / "npu",
            build_dir=SIM_BUILD,
            plusargs=[
                f"+KERNEL_BIN={kernel}",
                f"+KERNEL_ENTRY=0x{entry:x}",
                f"+SHAPE_M={shape[0]}",
                f"+SHAPE_N={shape[1]}",
                f"+SHAPE_K={shape[2]}",
            ],
            seed=1,
        )
        tree = ET.parse(results)
        problems = tree.findall(".//failure") + tree.findall(".//error")
        if problems:
            print(
                f"FAIL npu_rtl_gemm {shape[0]}x{shape[1]}x{shape[2]}: "
                f"{problems[0].get('message', 'test failed')}"
            )
            return 1
        print(f"PASS npu_rtl_gemm {shape[0]}x{shape[1]}x{shape[2]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
