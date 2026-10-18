#!/usr/bin/env python3
"""FP32 unit and serialized MMA RTL tests."""

from __future__ import annotations

import shutil
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from cocotb_tools.runner import get_runner

ROOT = Path(__file__).resolve().parents[2]


def run_test(name, sources, top, module, test_dir, includes, build_dir):
    build = ROOT / "build" / build_dir
    if build.exists():
        shutil.rmtree(build)
    runner = get_runner("verilator")
    runner.build(
        hdl_library=f"{name}_lib",
        sources=sources,
        hdl_toplevel=top,
        build_dir=build,
        includes=includes,
        build_args=["--timing", "-Wno-fatal"],
        always=True,
    )
    results = runner.test(
        hdl_toplevel=top,
        hdl_toplevel_library=f"{name}_lib",
        test_module=module,
        test_dir=test_dir,
        build_dir=build,
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
    ok &= run_test(
        "fp32",
        [ROOT / "sim" / "fp" / "clover_fp_tb.sv"],
        "clover_fp_tb",
        "test_fp",
        ROOT / "sim" / "fp",
        [ROOT / "rtl" / "fp"],
        "fp",
    )
    ok &= run_test(
        "tile_mma",
        [ROOT / "sim" / "tile" / "clover_tile_mma_tb.sv"],
        "clover_tile_mma_tb",
        "test_tile_mma",
        ROOT / "sim" / "tile",
        [
            ROOT / "rtl" / "common",
            ROOT / "rtl" / "fp",
            ROOT / "rtl" / "tile",
            ROOT / "rtl" / "mem",
            ROOT / "sim" / "tile",
        ],
        "tile_mma",
    )
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
