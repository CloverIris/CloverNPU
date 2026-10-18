#!/usr/bin/env python3
"""Run the C test programs from scratchpad through the RTL core."""

from __future__ import annotations

import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from cocotb_tools.runner import get_runner

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "cmake" / "bin"
WORK = ROOT / "build" / "core_mem"
SIM_BUILD = WORK / "sim"

CASES = [
    ("add.c", 7, ""),
    ("fib.c", 55, ""),
    ("hello.c", 0, "Hi\n"),
    ("array.c", 30, ""),
    ("ptrs.c", 10, ""),
    ("bigarray.c", 43, ""),
]


def main() -> int:
    if not (BIN / "cloverc").exists():
        print("missing build/cmake/bin; run `make tools` first", file=sys.stderr)
        return 1
    WORK.mkdir(parents=True, exist_ok=True)
    if SIM_BUILD.exists():
        shutil.rmtree(SIM_BUILD)

    runner = get_runner("verilator")
    runner.build(
        hdl_library="clover_core_mem_lib",
        sources=[ROOT / "rtl" / "top" / "clover_core_mem.sv"],
        hdl_toplevel="clover_core_mem",
        build_dir=SIM_BUILD,
        includes=[
            ROOT / "rtl" / "common",
            ROOT / "rtl" / "core",
            ROOT / "rtl" / "mem",
            ROOT / "rtl" / "top",
        ],
        build_args=["--timing", "-Wno-fatal"],
        always=True,
    )

    failures = 0
    for source, expected_exit, expected_stdout in CASES:
        stem = Path(source).stem
        image = WORK / f"{stem}.bin"
        compiled = subprocess.run(
            [
                str(BIN / "cloverc"),
                str(ROOT / "tests" / "m2" / source),
                "--base",
                "0",
                "-o",
                str(image),
            ],
            text=True,
            capture_output=True,
        )
        if compiled.returncode != 0:
            print(f"FAIL {source}: compile failed\n{compiled.stderr}")
            failures += 1
            continue

        results = runner.test(
            hdl_toplevel="clover_core_mem",
            hdl_toplevel_library="clover_core_mem_lib",
            test_module="test_core_mem",
            test_dir=ROOT / "sim" / "mem",
            build_dir=SIM_BUILD,
            plusargs=[
                f"+PROG_BIN={image}",
                f"+EXPECT_EXIT={expected_exit}",
                f"+EXPECT_STDOUT_HEX={expected_stdout.encode().hex()}",
            ],
            seed=1,
        )
        tree = ET.parse(results)
        problems = tree.findall(".//failure") + tree.findall(".//error")
        if problems:
            print(f"FAIL {source}: {problems[0].get('message', 'test failed')}")
            failures += 1
            continue
        print(f"PASS {source}")

    print()
    if failures:
        print(f"{failures} scratchpad-core failure(s)")
        return 1
    print(f"all {len(CASES)} scratchpad-core tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
