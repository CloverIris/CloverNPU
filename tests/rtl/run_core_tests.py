#!/usr/bin/env python3
"""ISS/RTL differential smoke tests for the Clover-S scalar core."""

from __future__ import annotations

import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from cocotb_tools.runner import get_runner

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "cmake" / "bin"
WORK = ROOT / "build" / "rtl"
SIM_BUILD = WORK / "sim"

CASES = [
    ("add.c", 7, ""),
    ("fib.c", 55, ""),
    ("sum.c", 55, ""),
    ("hello.c", 0, "Hi\n"),
    ("array.c", 30, ""),
    ("global.c", 7, ""),
    ("chartest.c", 66, ""),
    ("pointer.c", 7, ""),
    ("args.c", 6, ""),
    ("logic.c", 1, ""),
    ("div.c", 16, ""),
    ("while.c", 10, ""),
    ("do.c", 10, ""),
    ("break.c", 8, ""),
    ("prototype.c", 42, ""),
    ("args6.c", 21, ""),
    ("ptrs.c", 10, ""),
    ("multidim.c", 6, ""),
    ("bigarray.c", 43, ""),
]


def run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, text=True, capture_output=True)


def bin_to_hex(image: Path, hex_path: Path) -> None:
    data = image.read_bytes()
    if len(data) % 4:
        data += b"\x00" * (4 - len(data) % 4)
    words = [
        int.from_bytes(data[i : i + 4], "little")
        for i in range(0, len(data), 4)
    ]
    hex_path.write_text("\n".join(f"{word:08x}" for word in words) + "\n")


def main() -> int:
    if not (BIN / "cloverc").exists():
        print("missing build/cmake/bin; run `make tools` first", file=sys.stderr)
        return 1
    WORK.mkdir(parents=True, exist_ok=True)
    if SIM_BUILD.exists():
        shutil.rmtree(SIM_BUILD)

    runner = get_runner("verilator")
    runner.build(
        hdl_library="clover_core_lib",
        sources=[ROOT / "sim" / "rtl" / "clover_core_tb.sv"],
        hdl_toplevel="clover_core_tb",
        build_dir=SIM_BUILD,
        includes=[ROOT / "rtl" / "common", ROOT / "rtl" / "core"],
        build_args=["--timing", "-Wno-fatal"],
        always=True,
    )

    failures = 0
    for source, expected_exit, expected_stdout in CASES:
        stem = Path(source).stem
        image = WORK / f"{stem}.bin"
        hex_path = WORK / f"{stem}.hex"
        compiled = run(
            [
                str(BIN / "cloverc"),
                str(ROOT / "tests" / "m2" / source),
                "-o",
                str(image),
            ]
        )
        if compiled.returncode != 0:
            print(f"FAIL {source}: compile failed\n{compiled.stderr}")
            failures += 1
            continue
        bin_to_hex(image, hex_path)

        results = runner.test(
            hdl_toplevel="clover_core_tb",
            hdl_toplevel_library="clover_core_lib",
            test_module="test_core",
            test_dir=ROOT / "sim" / "rtl",
            build_dir=SIM_BUILD,
            plusargs=[
                f"+PROG={hex_path}",
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
        print(f"{failures} RTL failure(s)")
        return 1
    print(f"all {len(CASES)} RTL core tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
