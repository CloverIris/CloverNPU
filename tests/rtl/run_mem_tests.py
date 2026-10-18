#!/usr/bin/env python3
"""cocotb tests for the M3 memory/bus blocks."""

from __future__ import annotations

import shutil
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from cocotb_tools.runner import get_runner

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build" / "mem"


def run_test(name: str, sources: list[Path], top: str, test_module: str,
             test_dir: Path, includes: list[Path]) -> bool:
    runner = get_runner("verilator")
    sim_build = BUILD / name
    if sim_build.exists():
        shutil.rmtree(sim_build)
    runner.build(
        hdl_library=f"{name}_lib",
        sources=sources,
        hdl_toplevel=top,
        build_dir=sim_build,
        includes=includes,
        build_args=["--timing", "-Wno-fatal"],
        always=True,
    )
    results = runner.test(
        hdl_toplevel=top,
        hdl_toplevel_library=f"{name}_lib",
        test_module=test_module,
        test_dir=test_dir,
        build_dir=sim_build,
        seed=1,
    )
    tree = ET.parse(results)
    problems = tree.findall(".//failure") + tree.findall(".//error")
    if problems:
        print(f"FAIL {name}: {problems[0].get('message', 'test failed')}")
        return False
    print(f"PASS {name}")
    return True


def main() -> int:
    ok = True
    common = [ROOT / "rtl" / "common", ROOT / "rtl" / "mem"]
    ok &= run_test(
        "scratchpad",
        [ROOT / "rtl" / "mem" / "clover_scratchpad.sv"],
        "clover_scratchpad",
        "test_scratchpad",
        ROOT / "sim" / "mem",
        common,
    )
    ok &= run_test(
        "axi_csr",
        [ROOT / "rtl" / "top" / "clover_axi_csr.sv"],
        "clover_axi_csr",
        "test_axi_csr",
        ROOT / "sim" / "bus",
        [
            ROOT / "rtl" / "common",
            ROOT / "rtl" / "bus",
            ROOT / "rtl" / "top",
        ],
    )
    ok &= run_test(
        "dma",
        [ROOT / "sim" / "dma" / "clover_dma_tb.sv"],
        "clover_dma_tb",
        "test_dma",
        ROOT / "sim" / "dma",
        [
            ROOT / "rtl" / "common",
            ROOT / "rtl" / "dma",
            ROOT / "rtl" / "mem",
            ROOT / "sim" / "dma",
        ],
    )
    ok &= run_test(
        "cmd_proc",
        [ROOT / "sim" / "cmd" / "clover_cp_tb.sv"],
        "clover_cp_tb",
        "test_cp",
        ROOT / "sim" / "cmd",
        [
            ROOT / "rtl" / "common",
            ROOT / "rtl" / "cmd",
            ROOT / "rtl" / "dma",
            ROOT / "rtl" / "mem",
            ROOT / "sim" / "dma",
            ROOT / "sim" / "cmd",
        ],
    )
    ok &= run_test(
        "npu_mem",
        [ROOT / "sim" / "npu" / "clover_npu_mem_tb.sv"],
        "clover_npu_mem_tb",
        "test_npu_mem",
        ROOT / "sim" / "npu",
        [
            ROOT / "rtl" / "common",
            ROOT / "rtl" / "top",
            ROOT / "rtl" / "bus",
            ROOT / "rtl" / "cmd",
            ROOT / "rtl" / "dma",
            ROOT / "rtl" / "mem",
            ROOT / "rtl" / "core",
            ROOT / "sim" / "dma",
            ROOT / "sim" / "npu",
        ],
    )
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
