#!/usr/bin/env python3
"""M2 software vertical-slice tests: assembler + ISS + cloverc."""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "cmake" / "bin"
WORK = ROOT / "build" / "m2"
SRC = ROOT / "tests" / "m2"


def run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, text=True, capture_output=True, **kwargs)


def main() -> int:
    if not (BIN / "clover-as").exists():
        print("missing build; run `make tools` first", file=sys.stderr)
        return 1
    if WORK.exists():
        shutil.rmtree(WORK)
    WORK.mkdir(parents=True)

    # (source, expected exit, expected stdout)
    cases = [
        ("sum.s", 55, ""),
        ("hello.s", 0, "Hi\n"),
        ("call.s", 42, ""),
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

    failures = 0
    for source, expected_exit, expected_stdout in cases:
        path = SRC / source
        stem = path.stem
        image = WORK / f"{stem}.bin"
        symbol_map = WORK / f"{stem}.map"
        assembly = WORK / f"{stem}.s"

        if source.endswith(".s"):
            compile_cmd = [
                str(BIN / "clover-as"),
                str(path),
                "-o",
                str(image),
                "--map",
                str(symbol_map),
            ]
        else:
            compile_cmd = [
                str(BIN / "cloverc"),
                str(path),
                "-o",
                str(image),
                "--asm",
                str(assembly),
                "--map",
                str(symbol_map),
            ]

        compiled = run(compile_cmd)
        if compiled.returncode != 0:
            print(f"FAIL {source}: compile failed\n{compiled.stderr}")
            failures += 1
            continue

        executed = run(
            [
                str(BIN / "clover-iss"),
                "--image",
                str(image),
                "--quiet",
                "--max-steps",
                "1000000",
            ]
        )
        if executed.returncode != expected_exit or executed.stdout != expected_stdout:
            print(
                f"FAIL {source}: exit={executed.returncode} "
                f"(want {expected_exit}), stdout={executed.stdout!r} "
                f"(want {expected_stdout!r})"
            )
            if executed.stderr:
                print(executed.stderr)
            failures += 1
            continue

        # The disassembler must at least decode the entry instruction.
        dumped = run(
            [
                str(BIN / "clover-objdump"),
                "--map",
                str(symbol_map),
                "--length",
                "8",
                str(image),
            ]
        )
        if dumped.returncode != 0 or "_start" not in dumped.stdout:
            print(f"FAIL {source}: objdump failed")
            failures += 1
            continue

        print(f"PASS {source}")

    # Negative test: an undefined call must be rejected.
    bad = WORK / "bad.c"
    bad.write_text("int main(void) { return missing(1); }\n")
    result = run([str(BIN / "cloverc"), str(bad), "-o", str(WORK / "bad.bin")])
    if result.returncode == 0:
        print("FAIL negative: undefined function accepted")
        failures += 1
    else:
        print("PASS negative undefined-function")

    print()
    if failures:
        print(f"{failures} failure(s)")
        return 1
    print(f"all {len(cases) + 1} M2 tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
