#!/usr/bin/env python3
"""RTL graph: GEMM -> VEC ADD through CP/DMA/core/tile engines."""

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
WORK = ROOT / "build" / "rtl_graph"
SIM_BUILD = WORK / "sim"


def compile_kernel(source, base, entry, stem):
    image = WORK / f"{stem}.bin"
    map_path = WORK / f"{stem}.map"
    result = subprocess.run(
        [
            str(BIN / "cloverc"),
            str(source),
            "--no-start",
            "--base",
            hex(base),
            "--entry",
            entry,
            "--map",
            str(map_path),
            "-o",
            str(image),
        ],
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        raise RuntimeError(f"compile {stem} failed:\n{result.stderr}")
    for line in map_path.read_text().splitlines():
        match = re.match(rf"0x([0-9a-fA-F]+)\s+{entry}$", line.strip())
        if match:
            return image, int(match.group(1), 16)
    raise RuntimeError(f"{entry} entry symbol not found")


def main() -> int:
    if not (BIN / "cloverc").exists():
        print("missing build/cmake/bin; run `make tools` first", file=sys.stderr)
        return 1
    WORK.mkdir(parents=True, exist_ok=True)
    if SIM_BUILD.exists():
        shutil.rmtree(SIM_BUILD)

    try:
        gemm_image, gemm_entry = compile_kernel(
            ROOT / "kernels" / "gemm.c", 0, "gemm", "gemm"
        )
        add_image, add_entry = compile_kernel(
            ROOT / "kernels" / "add.c", 0x1000, "add", "add"
        )
    except RuntimeError as error:
        print(error)
        return 1

    runner = get_runner("verilator")
    runner.build(
        hdl_library="npu_graph_lib",
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
        hdl_toplevel_library="npu_graph_lib",
        test_module="test_npu_graph",
        test_dir=ROOT / "sim" / "npu",
        build_dir=SIM_BUILD,
        plusargs=[
            f"+KERNEL_BIN={gemm_image}",
            f"+KERNEL_ENTRY=0x{gemm_entry:x}",
            f"+ADD_BIN={add_image}",
            f"+ADD_ENTRY=0x{add_entry:x}",
        ],
        seed=1,
    )
    tree = ET.parse(results)
    problems = tree.findall(".//failure") + tree.findall(".//error")
    if problems:
        print(f"FAIL npu_rtl_graph: {problems[0].get('message', 'test failed')}")
        return 1
    print("PASS npu_rtl_graph")
    return 0


if __name__ == "__main__":
    sys.exit(main())
